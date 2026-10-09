// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parser.h"
#include <riwo/http/protocol/utils/core/parser.h>
#include <riwo/http/protocol/utils/core/compression.h>
#include <riwo/core/string_vector.h>

namespace riwo::http
{

class RIWO_DECL_HIDDEN parser<protocol_model::client>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(size_t init_buf_size) :
		m_parser(init_buf_size)
	{
		m_parser
		.read_until_eof()
		.on_parse_begin([this](std::string_view line_buf)
		{
			sys_expected<version_enum> result = static_cast<version_enum>(0);
			auto request_line_parts = string_vector::from_string(line_buf, ' ');

			if( request_line_parts.size() < 2 or
				not strtls::to_upper(request_line_parts[0]).starts_with("HTTP/") )
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_reply_line)
				);
			}
			try {
				result = version::from_string(request_line_parts[0].substr(5,3));
			}
			catch(const std::exception&)
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_reply_line)
				);
			}
			auto status_value = strtls::to_arith<status_enum>(request_line_parts[1]);
			if( not status_value or not status::check(*status_value, false) )
			{
				return result.despair (
					base_parser::make_error_code(parse_errc::invalid_status_code)
				);
			}
			m_status = *status_value;

			m_parser.skip_body (
				m_request_method == method::head or
				(m_status >= 100 and m_status < 200) or
				m_status == status::no_content or
				m_status == status::not_modified or
				(m_request_method == method::connect and m_status >= 200 and m_status < 300)
			);
			if( request_line_parts.size() > 2 )
				m_description = request_line_parts.join(2, ' ');
			else
				m_description = status::description(m_status, false);
			return result;
		})
		.on_parse_cookie([this](std::string_view line_buf)
		{
			auto vector = string_vector::from_string(line_buf, ';');
			if( vector.empty() )
				return base_parser::make_error_code(parse_errc::invalid_cookie_line);

			vector[0] = strtls::trimmed(vector[0]);
			auto pos = vector[0].find('=');

			if( pos == std::string::npos )
				return base_parser::make_error_code(parse_errc::invalid_cookie_line);

			auto key = strtls::trimmed(vector[0].substr(0, pos));
			auto cookie_name = key;

			auto value = strtls::trimmed(vector[0].substr(pos + 1));
			auto &cookie = m_cookies[std::move(key)] = std::move(value);

			// A received cookie without Path uses the request-path default.  The
			// cookie value type defaults Path to "/" for server-side generation,
			// so remove that construction default before parsing Set-Cookie.
			cookie.unset_path();

			for(size_t i=1; i<vector.size(); i++)
			{
				auto &statement = vector[i];
				statement = strtls::trimmed(statement);

				if( statement.empty() )
					continue;

				pos = statement.find('=');
				if( pos == std::string::npos )
				{
					cookie.set_attribute(std::move(statement), true);
					continue;
				}
				key = strtls::trimmed(statement.substr(0,pos));
				value = strtls::trimmed(statement.substr(pos+1));
				cookie.set_attribute(std::move(key), std::move(value));
			}
			m_set_cookies.emplace_back(std::move(cookie_name), cookie);
			return error_code();
		});
	}

public:
	[[nodiscard]] error_code set_attribute()
	{
		if( m_attributes_set )
			return {};
		m_attributes_set = true;

		const auto &response_headers = m_parser.headers();
		auto it = response_headers.find(header::connection);
		m_keep_alive = m_parser.version() != version::v10;

		if( it != response_headers.end() )
		{
			for(auto &str : string_vector::from_string(it->second.to_string(), ','))
			{
				if( auto value = strtls::to_lower(strtls::trimmed(str));
					value == "close" )
					m_keep_alive = false;

				else if( value == "keep-alive" )
					m_keep_alive = true;
			}
		}
		it = response_headers.find(header::content_encoding);
		if( it == response_headers.end() )
			m_support_gzip = false;
		else
		{
			auto codings = string_vector::from_string(it->second.to_string(), ',');
			for(auto &str : codings)
			{
				if( strtls::to_lower(strtls::trimmed(str)) == "gzip" )
				{
					m_support_gzip = true;
					break;
				}
			}
			if constexpr( gzip_available_v )
			{
				if( m_automatic_decompression and codings.size() == 1 and
					m_support_gzip and m_status != status::partial_content and
					m_parser.stage() == stage::body )
					m_gzip_decoder = std::make_unique<gzip_decoder>();
			}
		}
		if( m_status == status::range_not_satisfiable )
		{
			it = response_headers.find(header::content_range);
			if( it == response_headers.end() )
				return {};

			auto range = parse_content_range(it->second.to_string());
			if( not range or range->unit != "bytes" or range->satisfied )
				return base_parser::make_error_code(parse_errc::invalid_size_format);

			m_content_range = *range;
			return {};
		}
		if( m_status != status::partial_content )
			return {};

		it = response_headers.find(header::content_type);
		if( it != response_headers.end() )
		{
			auto content_type = it->second.to_string();
			auto pos = content_type.find(';');

			auto media_type = strtls::to_lower (
				strtls::trimmed(content_type.substr(0, pos))
			);
			if( media_type == "multipart/byteranges" )
			{
				auto boundary = parse_multipart_byte_ranges_boundary(content_type);
				if( not boundary )
					return base_parser::make_error_code(parse_errc::invalid_size_format);

				m_multipart_parser = std::make_unique<multipart_byte_ranges_parser>(*boundary);
				m_body_norms = multipart_body_norms {
					.boundary = *boundary, .packages = {}
				};
				return {};
			}
		}
		it = response_headers.find(header::content_range);
		if( it == response_headers.end() )
			return base_parser::make_error_code(parse_errc::invalid_size_format);

		auto range = parse_content_range(it->second.to_string());
		if( not range or range->unit != "bytes" or not range->satisfied )
			return base_parser::make_error_code(parse_errc::invalid_size_format);

		m_content_range = *range;
		m_body_norms = range_body_norms {
			.begin = range->first,
			.total = range->length()
		};
		return {};
	}

	[[nodiscard]] error_code consume_body()
	{
		auto body = m_parser.take_body();
		if( m_gzip_decoder )
		{
			auto decoded = m_gzip_decoder->append (
				body, m_parser.stage() == stage::finished
			);
			if( not decoded )
				return base_parser::make_error_code(parse_errc::invalid_size_format);

			body = std::move(*decoded);
			m_content_decoded = true;
		}
		if( m_multipart_parser )
		{
			if( not body.empty() )
			{
				auto chunks = m_multipart_parser->append(body);
				if( not chunks )
					return base_parser::make_error_code(parse_errc::invalid_size_format);

				for(auto &[part_index, offset, data] : *chunks)
					append_body(part_index, offset, data);
				sync_multipart_norms();
			}
			if( m_parser.stage() == stage::finished )
			{
				if( auto error = m_multipart_parser->finish(); error )
					return base_parser::make_error_code(parse_errc::invalid_size_format);
				sync_multipart_norms();
			}
			return {};
		}
		if( not body.empty() )
		{
			auto offset = m_plain_body_size;
			if( m_content_range and m_content_range->satisfied )
			{
				if( body.size() > m_content_range->length() -
					std::min(m_plain_body_size, m_content_range->length()) )
					return base_parser::make_error_code(parse_errc::invalid_size_format);
				offset += m_content_range->first;
			}
			m_plain_body_size += body.size();
			append_body(0, offset, std::move(body));
		}
		if( m_parser.stage() == stage::finished and m_content_range and
			m_content_range->satisfied and m_plain_body_size != m_content_range->length() )
			return base_parser::make_error_code(parse_errc::invalid_size_format);
		return {};
	}

	[[nodiscard]] std::string_view partial_body() const noexcept {
		return std::string_view(m_partial_body).substr(m_partial_body_pos);
	}

	[[nodiscard]] bool partial_body_empty() const noexcept {
		return m_partial_body_pos == m_partial_body.size();
	}

	void clear_partial_body() noexcept
	{
		m_partial_body.clear();
		m_partial_body_pos = 0;
	}

	void compact_partial_body()
	{
		if( partial_body_empty() )
			clear_partial_body();

		else if( m_partial_body_pos >= default_parser_buffer_size and
				 m_partial_body_pos >= m_partial_body.size() - m_partial_body_pos )
		{
			m_partial_body.erase(0, m_partial_body_pos);
			m_partial_body_pos = 0;
		}
	}

	void append_segment(size_t part_index, size_t offset, size_t size)
	{
		if( size == 0 )
			return ;

		if( not m_segments.empty() and m_segments.back().part_index == part_index and
			m_segments.back().offset + m_segments.back().length == offset )
			m_segments.back().length += size;
		else
		{
			m_segments.emplace_back(segment {
				.part_index = part_index,
				.offset = offset,
				.length = size
			});
		}
	}

	void append_body(size_t part_index, size_t offset, const std::string &data)
	{
		if( data.empty() )
			return ;

		append_segment(part_index, offset, data.size());
		compact_partial_body();
		m_partial_body += data;
	}

	void append_body(size_t part_index, size_t offset, std::string &&data)
	{
		if( data.empty() )
			return ;

		append_segment(part_index, offset, data.size());
		if( partial_body_empty() )
		{
			m_partial_body = std::move(data);
			m_partial_body_pos = 0;
		}
		else
		{
			compact_partial_body();
			m_partial_body += data;
		}
	}

	void sync_multipart_norms()
	{
		auto &[boundary, packages] = std::get<multipart_body_norms>(m_body_norms);
		const auto &parts = m_multipart_parser->parts();

		while( packages.size() < parts.size() )
		{
			const auto &part = parts[packages.size()];
			auto &[package_headers, range] = packages.emplace_back();

			for(auto &[key,value] : part.fields)
				package_headers.emplace_back(key + ": " + value.to_string());

			range = {
				.begin = part.range.first,
				.total = part.range.length()
			};
		}
	}

	void consume_segments(size_t size) noexcept
	{
		while( size > 0 and not m_segments.empty() )
		{
			auto &current_segment = m_segments.front();
			auto consumed = std::min(size, current_segment.length);

			current_segment.offset += consumed;
			current_segment.length -= consumed;
			size -= consumed;

			if( current_segment.length == 0 )
				m_segments.pop_front();
		}
	}

	[[nodiscard]] std::string take_partial_body(size_t size)
	{
		size = std::min(size, partial_body().size());
		auto result = std::string(partial_body().substr(0, size));

		m_partial_body_pos += size;
		consume_segments(size);

		compact_partial_body();
		return result;
	}

	[[nodiscard]] size_t read_partial_body(const mutable_buffer &buffer) noexcept
	{
		auto size = std::min(buffer.size(), partial_body().size());
		if( size == 0 )
			return 0;

		m_partial_body.copy (
			static_cast<char*>(buffer.data()), size,
			m_partial_body_pos
		);
		m_partial_body_pos += size;
		consume_segments(size);

		compact_partial_body();
		return size;
	}

	[[nodiscard]] std::string take_all_partial_body()
	{
		std::string result;
		if( m_partial_body_pos == 0 )
			result = std::move(m_partial_body);
		else
			result = partial_body();

		clear_partial_body();
		m_segments.clear();
		return result;
	}

	[[nodiscard]] size_t prepare_direct_body_read(size_t size) const noexcept
	{
		if( m_gzip_decoder or m_multipart_parser or m_content_range or
			not partial_body_empty() )
			return 0;
		return m_parser.prepare_direct_body_read(size);
	}

	[[nodiscard]] bool commit_direct_body_read(size_t size) noexcept
	{
		if( m_gzip_decoder or m_multipart_parser or m_content_range or
			not partial_body_empty() or not m_parser.commit_direct_body_read(size) )
			return false;

		m_plain_body_size += size;
		return true;
	}

	[[nodiscard]] optional<byte_range_chunk> take_range_body(size_t size)
	{
		if( size == 0 or m_segments.empty() )
			return {};

		auto current_segment = m_segments.front();
		size = std::min(size, current_segment.length);

		byte_range_chunk result {
			.part_index = current_segment.part_index,
			.offset = current_segment.offset,
			.data = std::string(partial_body().substr(0, size))
		};
		m_partial_body_pos += size;
		consume_segments(size);

		compact_partial_body();
		return result;
	}

	void reset_range_state()
	{
		m_attributes_set = false;
		m_content_range.reset();
		m_multipart_parser.reset();

		m_body_norms = basic_body_norms {};
		clear_partial_body();
		m_segments.clear();

		m_plain_body_size = 0;
		m_gzip_decoder.reset();
		m_content_decoded = false;
	}

public:
	struct segment
	{
		size_t part_index;
		size_t offset;
		size_t length;
	};
	base_parser m_parser;
	status_enum m_status = status::none;

	std::string m_description = "None";
	cookies_t m_cookies {};

	set_cookie_values_t m_set_cookies {};
	body_norms_t m_body_norms {};

	optional<http::content_range> m_content_range {};
	std::unique_ptr<multipart_byte_ranges_parser> m_multipart_parser {};

	std::string m_partial_body {};
	size_t m_partial_body_pos = 0;

	std::deque<segment> m_segments {};
	size_t m_plain_body_size = 0;

	bool m_keep_alive = false;
	bool m_support_gzip = false;
	bool m_attributes_set = false;
	bool m_automatic_decompression = true;
	bool m_content_decoded = false;

	method_enum m_request_method = method::get;
	std::unique_ptr<gzip_decoder> m_gzip_decoder {};
};

parser<protocol_model::client>::parser(size_t init_buf_size) :
	const_headers(nullptr),
	const_cookies(nullptr),
	const_chunk_attributes(nullptr),
	m_impl(new impl(init_buf_size))
{
	m_headers = &m_impl->m_parser.headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_parser.chunk_attributes();
}

bool parser<protocol_model::client>::keep_alive() const noexcept
{
	return m_impl->m_keep_alive;
}

bool parser<protocol_model::client>::support_gzip() const noexcept
{
	return m_impl->m_support_gzip;
}

bool parser<protocol_model::client>::content_decoded() const noexcept
{
	return m_impl->m_content_decoded;
}

bool parser<protocol_model::client>::automatic_decompression() const noexcept
{
	return m_impl->m_automatic_decompression;
}

bool parser<protocol_model::client>::is_chunked() const noexcept
{
	auto it = headers().find(header::transfer_encoding);
	if( it == headers().end() )
		return false;

	return std::ranges::any_of (
		string_vector::from_string(it->second.to_string(), ','),
		[](const auto &coding) {
			return strtls::to_lower(strtls::trimmed(coding)) == "chunked";
		}
	);
}

bool parser<protocol_model::client>::is_range_response() const noexcept
{
	return m_impl->m_status == status::partial_content or
		m_impl->m_status == status::range_not_satisfiable;
}

bool parser<protocol_model::client>::is_multipart_byte_ranges() const noexcept
{
	return static_cast<bool>(m_impl->m_multipart_parser);
}

bool parser<protocol_model::client>::is_informational() const noexcept
{
	auto code = static_cast<uint16_t>(m_impl->m_status);
	return code >= 100 and code < 200 and
		m_impl->m_status != status::switching_protocols;
}

bool parser<protocol_model::client>::is_upgrade() const noexcept
{
	if( m_impl->m_status != status::switching_protocols or
		headers().find(header::upgrade) == headers().end() )
		return false;

	auto it = headers().find(header::connection);
	if( it == headers().end() )
		return false;

	return std::ranges::any_of (
		string_vector::from_string(it->second.to_string(), ','),
		[](const auto &token) {
			return strtls::to_lower(strtls::trimmed(token)) == "upgrade";
		}
	);
}

parser<protocol_model::client>&
parser<protocol_model::client>::set_request_method(method_enum request_method) noexcept
{
	m_impl->m_request_method = request_method;
	return *this;
}

parser<protocol_model::client>&
parser<protocol_model::client>::set_automatic_decompression(bool enabled) noexcept
{
	m_impl->m_automatic_decompression = enabled;
	return *this;
}

method_enum parser<protocol_model::client>::request_method() const noexcept
{
	return m_impl->m_request_method;
}

const optional<content_range> &parser<protocol_model::client>::content_range() const noexcept
{
	return m_impl->m_content_range;
}

optional<size_t> parser<protocol_model::client>::complete_length() const noexcept
{
	if( m_impl->m_content_range and m_impl->m_content_range->complete_length )
		return m_impl->m_content_range->complete_length;

	if( m_impl->m_multipart_parser )
	{
		for(auto &[fields, range] : m_impl->m_multipart_parser->parts())
		{
			if( range.complete_length )
				return range.complete_length;
		}
	}
	return {};
}

const body_norms_t &parser<protocol_model::client>::body_norms() const noexcept
{
	return m_impl->m_body_norms;
}

const parser<protocol_model::client>::set_cookie_values_t&
parser<protocol_model::client>::set_cookies() const noexcept
{
	return m_impl->m_set_cookies;
}

std::string parser<protocol_model::client>::take_partial_body(size_t size)
{
	return m_impl->take_partial_body(size);
}

size_t parser<protocol_model::client>::read_partial_body(const mutable_buffer &buffer) noexcept
{
	return m_impl->read_partial_body(buffer);
}

size_t parser<protocol_model::client>::partial_body_size() const noexcept
{
	return m_impl->partial_body().size();
}

std::string parser<protocol_model::client>::take_body()
{
	return m_impl->take_all_partial_body();
}

std::string parser<protocol_model::client>::take_pending_data()
{
	return m_impl->m_parser.take_pending_data();
}

optional<byte_range_chunk> parser<protocol_model::client>::take_range_body(size_t size)
{
	return m_impl->take_range_body(size);
}

size_t parser<protocol_model::client>::prepare_direct_body_read(size_t size) const noexcept
{
	return m_impl->prepare_direct_body_read(size);
}

bool parser<protocol_model::client>::commit_direct_body_read(size_t size) noexcept
{
	return m_impl->commit_direct_body_read(size);
}

parser<protocol_model::client>::~parser()
{
	delete m_impl;
}

parser<protocol_model::client>::parser(parser &&other) noexcept :
	const_headers(nullptr),
	const_cookies(nullptr),
	const_chunk_attributes(nullptr),
	m_impl(other.m_impl)
{
	m_headers = &m_impl->m_parser.headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_parser.chunk_attributes();

	other.m_impl = new impl(0);
	other.m_headers = &other.m_impl->m_parser.headers();
	other.m_cookies = &other.m_impl->m_cookies;
	other.m_chunk_attributes = &other.m_impl->m_parser.chunk_attributes();
}

parser<protocol_model::client> &parser<protocol_model::client>::operator=(parser &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	m_headers = &m_impl->m_parser.headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_parser.chunk_attributes();

	other.m_impl = new impl(0);
	other.m_headers = &other.m_impl->m_parser.headers();
	other.m_cookies = &other.m_impl->m_cookies;
	other.m_chunk_attributes = &other.m_impl->m_parser.chunk_attributes();
	return *this;
}

sys_expected<bool> parser<protocol_model::client>::append(const const_buffer &buf)
{
	auto expected = m_impl->m_parser.append(buf);
	if( not expected )
		return expected;

	if( m_impl->m_parser.stage() != stage::header )
	{
		if( auto error = m_impl->set_attribute() )
			return sys_unexpected(error);
		if( auto error = m_impl->consume_body() )
			return sys_unexpected(error);
	}
	return expected;
}

parser<protocol_model::client> &parser<protocol_model::client>::operator<<(const const_buffer &buf)
{
	append(buf);
	return *this;
}

sys_expected<bool> parser<protocol_model::client>::next_message()
{
	m_impl->m_status = status::none;
	m_impl->m_description = "None";

	m_impl->m_cookies.clear();
	m_impl->m_set_cookies.clear();

	m_impl->m_keep_alive = false;
	m_impl->m_support_gzip = false;

	m_impl->reset_range_state();
	auto expected = m_impl->m_parser.next_message();

	if( not expected )
		return expected;

	if( m_impl->m_parser.stage() != stage::header )
	{
		if( auto error = m_impl->set_attribute() )
			return sys_unexpected(error);
		if( auto error = m_impl->consume_body() )
			return sys_unexpected(error);
	}
	return expected;
}

bool parser<protocol_model::client>::finish_eof()
{
	if( not m_impl->m_parser.finish_eof() )
		return false;
	return not m_impl->consume_body();
}

version_enum parser<protocol_model::client>::version() const noexcept
{
	return m_impl->m_parser.version();
}

status_enum parser<protocol_model::client>::status() const noexcept
{
	return m_impl->m_status;
}

parser<protocol_model::client>::stage_t parser<protocol_model::client>::stage() const noexcept
{
	if( not m_impl->m_partial_body.empty() )
		return stage_t::body;
	return m_impl->m_parser.stage();
}

parser<protocol_model::client> &parser<protocol_model::client>::reset()
{
	m_impl->m_parser.reset();
	m_impl->m_status = status::none;
	m_impl->m_description = "None";

	m_impl->m_cookies.clear();
	m_impl->m_set_cookies.clear();

	m_impl->m_keep_alive = false;
	m_impl->m_support_gzip = false;

	m_impl->reset_range_state();
	return *this;
}

} //namespace riwo::http
