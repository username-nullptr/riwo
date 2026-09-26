// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parser.h"
#include <riwo/core/algorithm/misc.h>
#include <riwo/core/string_vector.h>

namespace riwo::http { namespace
{

constexpr size_t compact_threshold = default_parser_buffer_size;

} //namespace

class RIWO_DECL_HIDDEN parser<protocol_model::base>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(size_t init_buf_size) {
		m_src_buf.reserve(init_buf_size);
	}

	[[nodiscard]] std::string_view source() const noexcept {
		return std::string_view(m_src_buf).substr(m_src_pos);
	}

	[[nodiscard]] bool source_empty() const noexcept {
		return m_src_pos == m_src_buf.size();
	}

	void clear_source() noexcept
	{
		m_src_buf.clear();
		m_src_pos = 0;
	}

	void compact_source()
	{
		if( source_empty() )
			clear_source();

		else if( m_src_pos >= compact_threshold and
			m_src_pos >= m_src_buf.size() - m_src_pos )
		{
			m_src_buf.erase(0, m_src_pos);
			m_src_pos = 0;
		}
	}

	void append_source(const const_buffer &buffer)
	{
		compact_source();
		m_src_buf.append(static_cast<const char*>(buffer.data()), buffer.size());
	}

	void consume_source(size_t size)
	{
		assert(size <= source().size());
		m_src_pos += size;
		compact_source();
	}

	[[nodiscard]] std::string take_source()
	{
		std::string result(source());
		clear_source();
		return result;
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

		else if( m_partial_body_pos >= compact_threshold and
				 m_partial_body_pos >= m_partial_body.size() - m_partial_body_pos )
		{
			m_partial_body.erase(0, m_partial_body_pos);
			m_partial_body_pos = 0;
		}
	}

	void append_partial_body(std::string_view body)
	{
		if( not body.empty() )
		{
			compact_partial_body();
			m_partial_body.append(body.data(), body.size());
		}
	}

	void consume_partial_body(size_t size)
	{
		assert(size <= partial_body().size());
		m_partial_body_pos += size;
		compact_partial_body();
	}

	[[nodiscard]] std::string take_all_partial_body()
	{
		if( m_partial_body_pos == 0 )
		{
			auto result = std::move(m_partial_body);
			clear_partial_body();
			return result;
		}
		std::string result(partial_body());
		clear_partial_body();
		return result;
	}

public:
	[[nodiscard]] sys_expected<bool> parse_header()
	{
		sys_expected<bool> result = false;
		do {
			auto pos = source().find("\r\n");
			if( pos == std::string::npos )
			{
				if( source().size() < 8192 )
					break;
				else if( m_state == state::waiting_request )
					result.despair(make_error_code(parse_errc::req_line_too_long));
				else if( m_state == state::reading_headers )
					result.despair(make_error_code(parse_errc::header_line_too_long));
				break;
			}
			auto line_buf = std::string(source().substr(0, pos));
			consume_source(pos + 2);

			if( m_state == state::waiting_request )
			{
				if( not m_parse_begin )
				{
					runtime_error::loc_throw (
						"riwo::http::parser: state_handler_waiting_begin == NULL."
					);
				}
				m_parse_begin(line_buf)
				.transform([&](version_enum version)
				{
					m_version = version;
					m_state = state::reading_headers;
				})
				.or_else([&](const error_code &error)
				{
					result.despair(error);
					reset();
				});
			}
			else if( m_state == state::reading_headers )
			{
				auto expected = state_handler_reading_headers(line_buf);
				if( not expected )
				{
					result.despair(expected.error());
					break;
				}
				result = *expected;
				if( result and *result )
					break;
			}
		}
		while( not source_empty() );
		return result;
	}

	[[nodiscard]] sys_expected<bool> state_handler_reading_headers(std::string_view line_buf)
	{
		sys_expected<bool> result = false;
		if( line_buf.empty() )
		{
			if( auto error = set_read_body_state() )
				result.despair(error);
			else
				result = true;
			return result;
		}
		auto colon_index = line_buf.find(':');
		if( colon_index == std::string::npos )
		{
			reset();
			return result.despair (
				make_error_code(parse_errc::invalid_header_line)
			);
		}
		auto field_name = line_buf.substr(0, colon_index);
		if( not valid_field_name(field_name) )
		{
			reset();
			return result.despair (
				make_error_code(parse_errc::invalid_header_line)
			);
		}
		auto error = header_insert (
			strtls::to_lower(field_name),
			strtls::trimmed(line_buf.substr(colon_index + 1))
		);
		if( error )
		{
			result.despair(error);
			reset();
		}
		return result;
	}

	[[nodiscard]] error_code set_read_body_state()
	{
		error_code error;
		if( m_skip_body )
		{
			m_state = state::finished;
			return error;
		}
		auto content_length = m_headers.find(header::content_length);
		auto transfer_encoding = m_headers.find(header::transfer_encoding);

		if( content_length != m_headers.end() and transfer_encoding != m_headers.end() )
			return make_error_code(parse_errc::invalid_size_format);

		if( content_length != m_headers.end() )
		{
			auto expected = content_length->second.get<size_t>();
			if( not expected )
				return make_error_code(parse_errc::invalid_size_format);

			m_content_length = *expected;
			parse_length();
		}
		else if( transfer_encoding != m_headers.end() )
		{
			if( m_version != version::v11 )
				return make_error_code(parse_errc::invalid_size_format);

			auto codings = string_vector::from_string(transfer_encoding->second.to_string(), ',');
			if( codings.size() != 1 or strtls::to_lower(strtls::trimmed(codings.back())) != "chunked" )
				return make_error_code(parse_errc::invalid_size_format);

			m_state = state::chunked_wait_size;
			parse_chunked().or_else([&](const error_code &e) {
				error = e;
			});
		}
		else if( m_read_until_eof )
		{
			m_state = state::reading_eof;
			if( not source_empty() )
			{
				append_partial_body(source());
				clear_source();
			}
		}
		else
			m_state = state::finished;
		return error;
	}

	void parse_length() noexcept
	{
		auto rsize = m_content_length - m_content_length_counter;
		if( rsize > source().size() )
			rsize = source().size();

		m_content_length_counter += rsize;
		append_partial_body(source().substr(0, rsize));
		consume_source(rsize);

		m_state = m_content_length_counter == m_content_length ?
			state::finished : state::reading_length;
	}

	[[nodiscard]] static bool is_token_char(uint8_t ch) noexcept
	{
		constexpr std::string_view c_table = "!#$%&'*+-.^_`|~";
		return (ch >= '0' and ch <= '9') or
			   (ch >= 'A' and ch <= 'Z') or
			   (ch >= 'a' and ch <= 'z') or
			   c_table.find(static_cast<char>(ch)) != std::string_view::npos;
	}

	[[nodiscard]] static bool valid_field_name(std::string_view value) noexcept
	{
		return not value.empty() and std::ranges::all_of(value, [](char ch) {
			return is_token_char(static_cast<uint8_t>(ch));
		});
	}

	[[nodiscard]] static bool is_quoted_char(uint8_t ch) noexcept
	{
		return ch == '\t' or ch == ' ' or ch == 0x21 or
			(ch >= 0x23 and ch <= 0x5B) or
			(ch >= 0x5D and ch <= 0x7E) or ch >= 0x80;
	}

	[[nodiscard]] error_code parse_chunk_attributes(std::string_view line_buf)
	{
		chunk_attributes_t attributes {};
		size_t pos = 0;
		auto skip_bws = [&]() noexcept
		{
			while( pos < line_buf.size() and (line_buf[pos] == ' ' or line_buf[pos] == '\t') )
				++pos;
		};
		skip_bws();
		while( pos < line_buf.size() )
		{
			if( line_buf[pos] != ';' )
				return make_error_code(parse_errc::invalid_size_format);
			++pos;
			skip_bws();

			auto begin = pos;
			while( pos < line_buf.size() and is_token_char(static_cast<uint8_t>(line_buf[pos])) )
				++pos;

			if( begin == pos )
				return make_error_code(parse_errc::invalid_size_format);

			std::string attribute(line_buf.substr(begin, pos - begin));
			skip_bws();
			if( pos < line_buf.size() and line_buf[pos] == '=' )
			{
				attribute += '=';
				++pos;
				skip_bws();
				if( pos == line_buf.size() )
					return make_error_code(parse_errc::invalid_size_format);

				begin = pos;
				if( line_buf[pos] == '"' )
				{
					++pos;
					bool closed = false;
					while( pos < line_buf.size() )
					{
						auto ch = static_cast<uint8_t>(line_buf[pos++]);
						if( ch == '"' )
						{
							closed = true;
							break;
						}
						if( ch == '\\' )
						{
							if( pos == line_buf.size() )
								return make_error_code(parse_errc::invalid_size_format);

							ch = static_cast<uint8_t>(line_buf[pos++]);
							if( ch != '\t' and (ch < 0x20 or ch == 0x7F) )
								return make_error_code(parse_errc::invalid_size_format);
						}
						else if( not is_quoted_char(ch) )
							return make_error_code(parse_errc::invalid_size_format);
					}
					if( not closed )
						return make_error_code(parse_errc::invalid_size_format);
				}
				else
				{
					while( pos < line_buf.size() and is_token_char(static_cast<uint8_t>(line_buf[pos])) )
						++pos;

					if( begin == pos )
						return make_error_code(parse_errc::invalid_size_format);
				}
				attribute += line_buf.substr(begin, pos - begin);
				skip_bws();
			}
			if( pos < line_buf.size() and line_buf[pos] != ';' )
				return make_error_code(parse_errc::invalid_size_format);
			attributes.emplace(std::move(attribute));
		}
		for(auto &attribute : attributes)
			m_chunk_attributes.emplace(attribute);
		return {};
	}

	sys_expected<bool> parse_chunked()
	{
		sys_expected<bool> result = false;
		for(;;)
		{
			if( m_state == state::chunked_wait_size )
			{
				auto pos = source().find("\r\n");
				if( pos == std::string::npos )
				{
					if( source().size() > 8192 )
						result.despair(make_error_code(parse_errc::header_line_too_long));
					return result;
				}
				auto line_buf = std::string(source().substr(0, pos));
				consume_source(pos + 2);

				auto attributes_pos = line_buf.find(';');
				auto size_buf = line_buf.substr(0, attributes_pos);

				size_buf = strtls::trimmed(size_buf);
				if( size_buf.empty() or size_buf.size() > sizeof(size_t) * 2 )
				{
					result.despair(make_error_code(parse_errc::invalid_size_format));
					return result;
				}
				auto expected = strtls::to_arith<size_t>(size_buf, 16);
				if( not expected )
				{
					result.despair(make_error_code(parse_errc::invalid_size_format));
					return result;
				}
				if( attributes_pos != std::string::npos )
				{
					auto error = parse_chunk_attributes (
						std::string_view(line_buf).substr(attributes_pos)
					);
					if( error )
					{
						result.despair(error);
						return result;
					}
				}
				m_chunk_size = *expected;
				m_state = m_chunk_size == 0 ?
					state::chunked_wait_headers : state::chunked_wait_content;
				continue;
			}
			if( m_state == state::chunked_wait_content )
			{
				if( source_empty() )
					return result;

				auto size = std::min(m_chunk_size, source().size());
				append_partial_body(source().substr(0, size));
				consume_source(size);
				m_chunk_size -= size;

				if( m_chunk_size == 0 )
					m_state = state::chunked_wait_content_end;
				continue;
			}
			if( m_state == state::chunked_wait_content_end )
			{
				if( source().size() < 2 )
					return result;

				if( not source().starts_with("\r\n") )
				{
					result.despair(make_error_code(parse_errc::invalid_size_format));
					return result;
				}
				consume_source(2);
				m_state = state::chunked_wait_size;
				continue;
			}
			if( m_state == state::chunked_wait_headers )
			{
				auto pos = source().find("\r\n");
				if( pos == std::string::npos )
				{
					if( source().size() > 8192 )
						result.despair(make_error_code(parse_errc::header_line_too_long));
					return result;
				}
				auto line_buf = std::string(source().substr(0, pos));
				consume_source(pos + 2);

				if( line_buf.empty() )
				{
					m_state = state::finished;
					result = true;
					return result;
				}
				auto colon_index = line_buf.find(':');
				if( colon_index == std::string::npos )
				{
					result.despair(make_error_code(parse_errc::invalid_size_format));
					return result;
				}
				auto field_name = line_buf.substr(0, colon_index);
				if( not valid_field_name(field_name) )
				{
					result.despair(make_error_code(parse_errc::invalid_size_format));
					return result;
				}
				auto error = header_insert (
					strtls::to_lower(field_name),
					strtls::trimmed(line_buf.substr(colon_index + 1))
				);
				if( error )
				{
					result.despair(error);
					reset();
					return result;
				}
			}
		}
		return result;
	}

	[[nodiscard]] error_code header_insert(std::string key, std::string value)
	{
		if( key == "cookie" or key == "set-cookie" )
		{
			if( not m_parse_cookie )
			{
				runtime_error::loc_throw (
					"riwo::http::parser: state_handler_waiting_begin == NULL."
				);
			}
			return m_parse_cookie(value);
		}
		if( auto it = m_headers.find(key); it != m_headers.end() )
		{
			if( key == "content-length" )
				return make_error_code(parse_errc::invalid_size_format);
			it->second = it->second.to_string() + ", " + value;
		}
		else
			m_headers[std::move(key)] = std::move(value);
		return {};
	}

	void reset(bool preserve_input = false)
	{
		m_state = state::waiting_request;
		m_version = version::none;

		if( not preserve_input )
			clear_source();

		m_headers.clear();
		m_chunk_attributes.clear();
		clear_partial_body();

		m_content_length_counter = 0;
		m_content_length = 0;
		m_chunk_size = 0;
		m_skip_body = false;
	}

public:
	enum class state
	{
		waiting_request,          // GET /path HTTP/1.1\r\n
							      // HTTP/1.1 200 OK\r\n
		reading_headers,          // Key: Value\r\n
		reading_length,           // Fixed length (Content-Length: 9\r\n).
		reading_eof,              // Response body delimited by connection close.
		chunked_wait_size,        // 9\r\n
		chunked_wait_content,     // body
		chunked_wait_content_end, // \r\n
		chunked_wait_headers,     // Key: Value\r\n
		finished
	}
	m_state = state::waiting_request;
	std::string m_src_buf {};
	size_t m_src_pos = 0;

	version_enum m_version = static_cast<version_enum>(0);
	headers_t m_headers {};

	chunk_attributes_t m_chunk_attributes {};
	std::string m_partial_body {};
	size_t m_partial_body_pos = 0;

	size_t m_content_length_counter = 0;
	size_t m_content_length = 0;
	size_t m_chunk_size = 0;

	bool m_skip_body = false;
	bool m_read_until_eof = false;

	parse_begin_handler m_parse_begin {};
	parse_cookie_handler m_parse_cookie {};
};

parser<protocol_model::base>::parser(size_t init_buf_size) :
	const_headers(nullptr),
	m_impl(new impl(init_buf_size))
{
	m_headers = &m_impl->m_headers;
}

parser<protocol_model::base>::~parser()
{
	delete m_impl;
}

parser<protocol_model::base>::parser(parser &&other) noexcept :
	const_headers(other.m_headers),
	m_impl(other.m_impl)
{
	other.m_impl = new impl(0);
	other.m_headers = &other.m_impl->m_headers;
}

parser<protocol_model::base> &parser<protocol_model::base>::operator=(parser &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	m_headers = other.m_headers;

	other.m_impl = new impl(0);
	other.m_headers = &other.m_impl->m_headers;
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::on_parse_begin(parse_begin_handler func)
{
	m_impl->m_parse_begin = std::move(func);
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::on_parse_cookie(parse_cookie_handler func)
{
	m_impl->m_parse_cookie = std::move(func);
	return *this;
}

error_code parser<protocol_model::base>::make_error_code(parse_errc errc)
{
	return http::make_error_code(errc);
}

sys_expected<bool> parser<protocol_model::base>::append(const const_buffer &buf)
{
	using state_t = impl::state;
	if( buf.size() == 0 )
		return { make_error_code(parse_errc::inserted_data_empty) };

	else if( m_impl->m_state == state_t::finished )
		return { make_error_code(parse_errc::request_end) };

	m_impl->append_source(buf);
	if( m_impl->m_state <= state_t::reading_headers )
		return m_impl->parse_header();

	else if( m_impl->m_state == state_t::reading_length )
	{
		m_impl->parse_length();
		return true;
	}
	else if( m_impl->m_state == state_t::reading_eof )
	{
		m_impl->append_partial_body(m_impl->source());
		m_impl->clear_source();
		return false;
	}
	return m_impl->parse_chunked();
}

parser<protocol_model::base> &parser<protocol_model::base>::operator<<(const const_buffer &buf)
{
	append(buf);
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::reset()
{
	m_impl->reset();
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::skip_body(bool value) noexcept
{
	m_impl->m_skip_body = value;
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::read_until_eof(bool value) noexcept
{
	m_impl->m_read_until_eof = value;
	return *this;
}

sys_expected<bool> parser<protocol_model::base>::next_message()
{
	m_impl->reset(true);
	if( m_impl->source_empty() )
		return false;
	return m_impl->parse_header();
}

bool parser<protocol_model::base>::finish_eof() noexcept
{
	if( m_impl->m_state != impl::state::reading_eof )
		return false;
	m_impl->m_state = impl::state::finished;
	return true;
}

std::string parser<protocol_model::base>::take_partial_body(size_t size)
{
	if( size == 0 )
		return {};
	else if( size > m_impl->partial_body().size() )
		size = m_impl->partial_body().size();

	auto res = std::string(m_impl->partial_body().substr(0, size));
	m_impl->consume_partial_body(size);
	return res;
}

size_t parser<protocol_model::base>::read_partial_body(const mutable_buffer &buffer) noexcept
{
	auto size = std::min(buffer.size(), m_impl->partial_body().size());
	if( size == 0 )
		return 0;

	m_impl->m_partial_body.copy (
		static_cast<char*>(buffer.data()), size,
		m_impl->m_partial_body_pos
	);
	m_impl->consume_partial_body(size);
	return size;
}

size_t parser<protocol_model::base>::partial_body_size() const noexcept
{
	return m_impl->partial_body().size();
}

std::string parser<protocol_model::base>::take_body()
{
	return m_impl->take_all_partial_body();
}

std::string parser<protocol_model::base>::take_pending_data()
{
	return m_impl->take_source();
}

size_t parser<protocol_model::base>::prepare_direct_body_read(size_t size) const noexcept
{
	if( size == 0 or m_impl->m_state != impl::state::reading_length or
		not m_impl->source_empty() or not m_impl->partial_body_empty() )
		return 0;

	auto remaining = m_impl->m_content_length - m_impl->m_content_length_counter;
	return std::min(size, remaining);
}

bool parser<protocol_model::base>::commit_direct_body_read(size_t size) noexcept
{
	if( m_impl->m_state != impl::state::reading_length or
		not m_impl->source_empty() or not m_impl->partial_body_empty() )
		return false;

	auto remaining = m_impl->m_content_length - m_impl->m_content_length_counter;
	if( size == 0 or size > remaining )
		return false;

	m_impl->m_content_length_counter += size;
	if( m_impl->m_content_length_counter == m_impl->m_content_length )
		m_impl->m_state = impl::state::finished;
	return true;
}

version_enum parser<protocol_model::base>::version() const noexcept
{
	return m_impl->m_version;
}

parser<protocol_model::base>::stage_t parser<protocol_model::base>::stage() const noexcept
{
	if( m_impl->m_state <= impl::state::reading_headers )
		return stage_t::header;
	else if( m_impl->m_state > impl::state::reading_headers and m_impl->m_state < impl::state::finished )
		return stage_t::body;
	else if( m_impl->partial_body_empty() )
		return stage_t::finished;
	return stage_t::body;
}

const parser<protocol_model::base>::chunk_attributes_t&
parser<protocol_model::base>::chunk_attributes() const noexcept
{
	return m_impl->m_chunk_attributes;
}

parser<protocol_model::base> &parser<protocol_model::base>::unbind_parse_begin()
{
	m_impl->m_parse_begin = {};
	return *this;
}

parser<protocol_model::base> &parser<protocol_model::base>::unbind_parse_cookie()
{
	m_impl->m_parse_cookie = {};
	return *this;
}

} //namespace riwo::http
