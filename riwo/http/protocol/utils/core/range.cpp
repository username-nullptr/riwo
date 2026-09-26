// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "range.h"
#include <riwo/core/algorithm/misc.h>

namespace riwo::http { namespace
{

[[nodiscard]] error_code invalid_argument_error() noexcept {
	return std::make_error_code(std::errc::invalid_argument);
}

[[nodiscard]] error_code result_out_of_range_error() noexcept {
	return std::make_error_code(std::errc::result_out_of_range);
}

[[nodiscard]] bool is_token_char(uint8_t ch) noexcept
{
	constexpr std::string_view c_table = "!#$%&'*+-.^_`|~";
	return (ch >= '0' and ch <= '9') or
		   (ch >= 'A' and ch <= 'Z') or
		   (ch >= 'a' and ch <= 'z') or
		   c_table.find(static_cast<char>(ch)) != std::string_view::npos;
}

[[nodiscard]] bool is_boundary_char(uint8_t ch) noexcept
{
	constexpr std::string_view c_table = "'()+_,-./:=? ";
	return (ch >= '0' and ch <= '9') or
		   (ch >= 'A' and ch <= 'Z') or
		   (ch >= 'a' and ch <= 'z') or
		   c_table.find(static_cast<char>(ch)) != std::string_view::npos;
}

[[nodiscard]] sys_expected<size_t> parse_decimal(std::string_view value) noexcept
{
	if( value.empty() )
		return sys_unexpected(invalid_argument_error());

	size_t result = 0;
	for(auto ch : value)
	{
		if( ch < '0' or ch > '9' )
			return sys_unexpected(invalid_argument_error());

		auto digit = static_cast<size_t>(ch - '0');
		if( result > (std::numeric_limits<size_t>::max() - digit) / 10 )
			return sys_unexpected(result_out_of_range_error());
		result = result * 10 + digit;
	}
	return result;
}

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] sys_expected<headers> parse_part_headers(std::string_view value) noexcept
{
	headers result {};
	while( not value.empty() )
	{
		auto pos = value.find("\r\n");
		auto line = pos == std::string_view::npos ? value : value.substr(0, pos);

		if( pos == std::string_view::npos )
			value = {};
		else
			value.remove_prefix(pos + 2);

		auto colon = line.find(':');
		if( colon == std::string_view::npos )
			return sys_unexpected(invalid_argument_error());

		auto key = trim_ows(line.substr(0, colon));
		if( key.empty() )
			return sys_unexpected(invalid_argument_error());

		for(auto ch : key)
		{
			if( not is_token_char(static_cast<uint8_t>(ch)) )
				return sys_unexpected(invalid_argument_error());
		}
		result[std::string(key)] =
			std::string(trim_ows(line.substr(colon + 1)));
	}
	return result;
}

} //namespace

size_t content_range::length() const noexcept
{
	return satisfied and last >= first ? last - first + 1 : 0;
}

sys_expected<ranges_specifier> parse_range_header(std::string_view value) noexcept
{
	value = trim_ows(value);
	auto equal = value.find('=');

	if( equal == std::string_view::npos )
		return sys_unexpected(invalid_argument_error());

	auto unit = trim_ows(value.substr(0, equal));
	if( unit.empty() )
		return sys_unexpected(invalid_argument_error());

	for(auto ch : unit)
	{
		if( not is_token_char(static_cast<uint8_t>(ch)) )
			return sys_unexpected(invalid_argument_error());
	}
	ranges_specifier result {
		.unit = strtls::to_lower(std::string(unit)),
		.ranges = {}
	};
	value.remove_prefix(equal + 1);
	for(;;)
	{
		auto comma = value.find(',');
		auto item = trim_ows(value.substr(0, comma));
		if( item.empty() )
			return sys_unexpected(invalid_argument_error());

		auto dash = item.find('-');
		if( dash == std::string_view::npos or item.find('-', dash + 1) != std::string_view::npos )
			return sys_unexpected(invalid_argument_error());

		auto first = trim_ows(item.substr(0, dash));
		auto last = trim_ows(item.substr(dash + 1));
		byte_range_spec spec;

		if( first.empty() )
		{
			auto length = parse_decimal(last);
			if( not length )
				return sys_unexpected(length.error());

			spec.form = byte_range_spec::form_t::suffix;
			spec.first = *length;
		}
		else
		{
			auto first_value = parse_decimal(first);
			if( not first_value )
				return sys_unexpected(first_value.error());

			spec.first = *first_value;
			if( last.empty() )
				spec.form = byte_range_spec::form_t::open_ended;
			else
			{
				auto last_value = parse_decimal(last);
				if( not last_value )
					return sys_unexpected(last_value.error());

				if( *last_value < spec.first )
					return sys_unexpected(invalid_argument_error());

				spec.form = byte_range_spec::form_t::closed;
				spec.last = *last_value;
			}
		}
		result.ranges.emplace_back(spec);
		if( comma == std::string_view::npos )
			break;
		value.remove_prefix(comma + 1);
	}
	return result;
}

file_ranges resolve_byte_ranges(const ranges_specifier &specifier, size_t complete_length) noexcept
{
	file_ranges result {};
	if( specifier.unit != "bytes" or complete_length == 0 )
		return result;

	for(auto &spec : specifier.ranges)
	{
		file_range range;
		if( spec.form == byte_range_spec::form_t::suffix )
		{
			if( spec.first == 0 )
				continue;

			range.total = std::min(spec.first, complete_length);
			range.begin = complete_length - range.total;
		}
		else
		{
			if( spec.first >= complete_length )
				continue;

			range.begin = spec.first;
			auto last = spec.form == byte_range_spec::form_t::open_ended ?
				complete_length - 1 : std::min(spec.last, complete_length - 1);

			range.total = last - range.begin + 1;
		}
		result.emplace_back(range);
	}
	return result;
}

sys_expected<content_range> parse_content_range(std::string_view value) noexcept
{
	value = trim_ows(value);
	auto space = value.find_first_of(" \t");

	if( space == std::string_view::npos )
		return sys_unexpected(invalid_argument_error());

	auto unit = value.substr(0, space);
	if( unit.empty() )
		return sys_unexpected(invalid_argument_error());

	for(auto ch : unit)
	{
		if( not is_token_char(static_cast<uint8_t>(ch)) )
			return sys_unexpected(invalid_argument_error());
	}
	value = trim_ows(value.substr(space + 1));

	content_range result {};
	result.unit = strtls::to_lower(std::string(unit));

	if( value.starts_with("*/") )
	{
		auto complete = parse_decimal(value.substr(2));
		if( not complete )
			return sys_unexpected(complete.error());

		result.complete_length = *complete;
		return result;
	}
	auto dash = value.find('-');
	auto slash = value.find('/', dash == std::string_view::npos ? 0 : dash + 1);

	if( dash == std::string_view::npos or slash == std::string_view::npos )
		return sys_unexpected(invalid_argument_error());

	auto first = parse_decimal(value.substr(0, dash));
	auto last = parse_decimal(value.substr(dash + 1, slash - dash - 1));

	if( not first )
		return sys_unexpected(first.error());

	if( not last )
		return sys_unexpected(last.error());

	if( *last < *first )
		return sys_unexpected(invalid_argument_error());

	result.satisfied = true;
	result.first = *first;
	result.last = *last;

	if( result.first == 0 and result.last == std::numeric_limits<size_t>::max() )
		return sys_unexpected(result_out_of_range_error());

	auto complete_value = value.substr(slash + 1);
	if( complete_value != "*" )
	{
		auto complete = parse_decimal(complete_value);
		if( not complete )
			return sys_unexpected(complete.error());

		if( *complete <= result.last )
			return sys_unexpected(invalid_argument_error());

		result.complete_length = *complete;
	}
	return result;
}

std::string format_content_range(const file_range &range, size_t complete_length)
{
	if( range.total == 0 or range.begin >= complete_length or
		range.total > complete_length - range.begin )
		return {};

	return std::format("bytes {}-{}/{}",
		range.begin, range.begin + range.total - 1, complete_length
	);
}

std::string format_unsatisfied_content_range(size_t complete_length)
{
	return std::format("bytes */{}", complete_length);
}

sys_expected<std::string>
parse_multipart_byte_ranges_boundary(std::string_view content_type) noexcept
{
	auto semicolon = content_type.find(';');
	auto media_type = strtls::to_lower(std::string(trim_ows(content_type.substr(0, semicolon))));

	if( media_type != "multipart/byteranges" )
		return sys_unexpected(invalid_argument_error());

	while( semicolon != std::string_view::npos )
	{
		content_type.remove_prefix(semicolon + 1);
		semicolon = content_type.find(';');

		auto parameter = trim_ows(content_type.substr(0, semicolon));
		auto equal = parameter.find('=');

		if( equal == std::string_view::npos )
			continue;

		if( auto name = strtls::to_lower(std::string(trim_ows(parameter.substr(0, equal))));
			name != "boundary" )
			continue;

		auto value = trim_ows(parameter.substr(equal + 1));
		std::string boundary {};
		bool quoted = false;

		if( not value.empty() and value.front() == '"' )
		{
			quoted = true;
			bool closed = false;

			for(size_t i=1; i<value.size(); ++i)
			{
				if( value[i] == '"' )
				{
					closed = i + 1 == value.size();
					break;
				}
				if( value[i] == '\\' )
				{
					if( ++i >= value.size() )
						return sys_unexpected(invalid_argument_error());
				}
				boundary += value[i];
			}
			if( not closed )
				return sys_unexpected(invalid_argument_error());
		}
		else
			boundary = value;

		if( boundary.empty() or boundary.size() > 70 or
			boundary.back() == ' ' )
			return sys_unexpected(invalid_argument_error());

		for(auto ch : boundary)
		{
			auto uch = static_cast<uint8_t>(ch);
			if( not is_boundary_char(uch) or (not quoted and not is_token_char(uch)) )
				return sys_unexpected(invalid_argument_error());
		}
		return boundary;
	}
	return sys_unexpected(invalid_argument_error());
}

class RIWO_DECL_HIDDEN multipart_byte_ranges_parser::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(std::string boundary) :
		m_boundary(std::move(boundary)),
		m_delimiter("--" + m_boundary),
		m_marker("\r\n" + m_delimiter) {}

	[[nodiscard]] std::string_view available_buffer() const noexcept {
		return std::string_view(m_buffer).substr(m_buffer_head);
	}

	[[nodiscard]] size_t available_size() const noexcept {
		return m_buffer.size() - m_buffer_head;
	}

	[[nodiscard]] bool buffer_empty() const noexcept {
		return m_buffer_head == m_buffer.size();
	}

	void consume(size_t size) noexcept {
		m_buffer_head += size;
	}

	void compact_buffer() noexcept
	{
		if( m_buffer_head == 0 )
			return ;

		if( m_buffer_head == m_buffer.size() )
		{
			m_buffer.clear();
			m_buffer_head = 0;
		}
		else if( m_buffer_head >= 64 * 1024 and m_buffer_head >= m_buffer.size() / 2 )
		{
			m_buffer.erase(0, m_buffer_head);
			m_buffer_head = 0;
		}
	}

	[[nodiscard]] static bool boundary_suffix_ready(std::string_view suffix) noexcept
	{
		if( suffix.starts_with("--") )
			return true;

		size_t padding = 0;
		while( padding < suffix.size() and
			(suffix[padding] == ' ' or suffix[padding] == '\t') )
			++padding;

		return suffix.size() >= padding + 2;
	}

	[[nodiscard]] sys_expected<std::vector<byte_range_chunk>>
	append(std::string_view data) noexcept
	{
		std::vector<byte_range_chunk> chunks;
		compact_buffer();

		m_buffer.append(data);
		for(;;)
		{
			if( m_state == state::preamble )
			{
				const auto buffer = available_buffer();
				auto pos = buffer.find(m_delimiter);

				if( pos == std::string::npos )
				{
					if( auto retained = m_delimiter.size() + 2; buffer.size() > retained )
						consume(buffer.size() - retained);
					break;
				}
				if( pos != 0 and (pos < 2 or buffer.substr(pos - 2, 2) != "\r\n") )
				{
					consume(pos + m_delimiter.size());
					continue;
				}
				auto suffix_pos = pos + m_delimiter.size();
				if( not boundary_suffix_ready(buffer.substr(suffix_pos)) )
					break;

				consume(pos + m_delimiter.size());
				if( not consume_boundary_suffix() )
					break;
				continue;
			}
			if( m_state == state::header_fields )
			{
				const auto buffer = available_buffer();
				auto pos = buffer.find("\r\n\r\n");

				if( pos == std::string::npos )
				{
					if( buffer.size() > 8192 )
						return sys_unexpected(invalid_argument_error());
					break;
				}
				auto parsed_headers = parse_part_headers(buffer.substr(0, pos));
				if( not parsed_headers )
					return sys_unexpected(parsed_headers.error());

				consume(pos + 4);
				auto it = parsed_headers->find(header::content_range);

				if( it == parsed_headers->end() )
					return sys_unexpected(invalid_argument_error());

				auto parsed_range = parse_content_range(it->second.to_string());
				if( not parsed_range or parsed_range->unit != "bytes" or not parsed_range->satisfied )
					return sys_unexpected(invalid_argument_error());

				if( parsed_range->complete_length )
				{
					if( m_complete_length and
						*m_complete_length != *parsed_range->complete_length )
						return sys_unexpected(invalid_argument_error());

					m_complete_length = parsed_range->complete_length;
				}
				m_parts.emplace_back(byte_range_part {
					.fields = std::move(*parsed_headers),
					.range = *parsed_range
				});
				m_part_consumed = 0;
				m_state = state::body;
				continue;
			}
			if( m_state == state::body )
			{
				auto &range = m_parts.back().range;
				auto remaining = range.length() - m_part_consumed;

				if( remaining == 0 )
				{
					m_state = state::boundary;
					continue;
				}
				if( buffer_empty() )
					break;

				const auto buffer = available_buffer();
				auto size = std::min(remaining, buffer.size());

				chunks.emplace_back(byte_range_chunk {
					.part_index = m_parts.size() - 1,
					.offset = range.first + m_part_consumed,
					.data = std::string(buffer.substr(0, size))
				});
				consume(size);
				m_part_consumed += size;
				continue;
			}
			if( m_state == state::boundary )
			{
				const auto buffer = available_buffer();
				if( buffer.size() < m_marker.size() )
					break;

				if( not boundary_suffix_ready(buffer.substr(m_marker.size())) )
					break;

				if( not buffer.starts_with(m_marker) )
					return sys_unexpected(invalid_argument_error());

				consume(m_marker.size());
				if( not consume_boundary_suffix() )
					break;
				continue;
			}
			if( m_state == state::closing )
			{
				if( not consume_closing_suffix() )
					break;
				continue;
			}
			break;
		}
		compact_buffer();
		return chunks;
	}

	[[nodiscard]] bool consume_boundary_suffix() noexcept
	{
		const auto buffer = available_buffer();
		if( buffer.starts_with("--") )
		{
			consume(2);
			m_state = state::closing;
			return consume_closing_suffix();
		}
		size_t padding = 0;
		while( padding < buffer.size() and (buffer[padding] == ' ' or buffer[padding] == '\t') )
			++padding;

		if( buffer.size() < padding + 2 )
			return false;

		if( buffer.substr(padding, 2) != "\r\n" )
		{
			m_state = state::failed;
			return true;
		}
		consume(padding + 2);
		m_state = state::header_fields;
		return true;
	}

	[[nodiscard]] bool consume_closing_suffix() noexcept
	{
		auto buffer = available_buffer();
		size_t padding = 0;

		while( padding < buffer.size() and (buffer[padding] == ' ' or buffer[padding] == '\t') )
			++padding;

		consume(padding);
		buffer = available_buffer();

		if( buffer.empty() or buffer == "\r" )
			return false;

		if( not buffer.starts_with("\r\n") )
		{
			m_state = state::failed;
			return true;
		}
		consume(2);
		m_state = state::finished;
		return true;
	}

public:
	enum class state
	{
		preamble,
		header_fields,
		body,
		boundary,
		closing,
		finished,
		failed
	}
	m_state = state::preamble;

	std::string m_boundary {};
	std::string m_delimiter {};
	std::string m_marker {};

	std::string m_buffer {};
	size_t m_buffer_head = 0;

	std::vector<byte_range_part> m_parts {};
	optional<size_t> m_complete_length {};

	size_t m_part_consumed = 0;
};

multipart_byte_ranges_parser::multipart_byte_ranges_parser(std::string boundary) :
	m_impl(new impl(std::move(boundary)))
{

}

multipart_byte_ranges_parser::~multipart_byte_ranges_parser()
{
	delete m_impl;
}

multipart_byte_ranges_parser::multipart_byte_ranges_parser
(multipart_byte_ranges_parser &&other) noexcept : m_impl(other.m_impl)
{
	other.m_impl = new impl({});
}

multipart_byte_ranges_parser &multipart_byte_ranges_parser::operator=
(multipart_byte_ranges_parser &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	other.m_impl = new impl({});
	return *this;
}

sys_expected<std::vector<byte_range_chunk>>
multipart_byte_ranges_parser::append(std::string_view data) noexcept
{
	return m_impl->append(data);
}

error_code multipart_byte_ranges_parser::finish() const noexcept
{
	if( m_impl->m_parts.empty() )
		return invalid_argument_error();

	if( finished() or (m_impl->m_state == impl::state::closing and m_impl->buffer_empty()) )
		return {};

	return invalid_argument_error();
}

bool multipart_byte_ranges_parser::finished() const noexcept
{
	return m_impl->m_state == impl::state::finished;
}

const std::vector<byte_range_part> &multipart_byte_ranges_parser::parts() const noexcept
{
	return m_impl->m_parts;
}

} //namespace riwo::http
