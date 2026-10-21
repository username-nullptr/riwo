// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "wire.h"
#include <charconv>

namespace riwo::http_exp::v1::detail { namespace
{

[[nodiscard]] bool equal_ci(std::string_view lhs,
	std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() and
		std::ranges::equal(lhs, rhs, [](unsigned char left, unsigned char right) {
			return std::tolower(left) == std::tolower(right);
		});
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool contains_token(std::string_view value,
	std::string_view token) noexcept
{
	while( not value.empty() )
	{
		auto comma = value.find(',');
		auto item = trim(value.substr(0, comma));
		if( equal_ci(item, token) ) return true;
		if( comma == std::string_view::npos ) break;
		value.remove_prefix(comma + 1);
	}
	return false;
}

[[nodiscard]] bool valid_host_value(std::string_view value) noexcept
{
	if( value.empty() ) return false;
	return std::ranges::none_of(value, [](unsigned char byte) {
		return byte <= 0x20U or byte == 0x7fU or byte == '/' or
			byte == '?' or byte == '#' or byte == '@';
	});
}

[[nodiscard]] std::string authority_from_url(const riwo::url &value)
{
	auto host = std::string(value.host());
	if( host.find(':') != std::string::npos and not host.starts_with('[') )
		host = '[' + host + ']';
	const auto scheme = std::string_view(value.protocol());
	const auto default_port = scheme == "https" ? 443 : 80;
	if( value.port() != 0 and value.port() != default_port )
		host += ':' + std::to_string(value.port());
	return host;
}

[[nodiscard]] sys_expected<protocol> parse_version(
	std::string_view value) noexcept
{
	if( value == "HTTP/1.1" ) return protocol::v1_1;
	if( value == "HTTP/1.0" ) return protocol::v1_0;
	return sys_unexpected(make_error_code(errc::connection_protocol_error));
}

[[nodiscard]] std::string_view version_text(protocol value) noexcept {
	return value == protocol::v1_0 ? "HTTP/1.0" : "HTTP/1.1";
}

[[nodiscard]] bool framing_header(std::string_view name) noexcept {
	return equal_ci(name, "Content-Length") or
		equal_ci(name, "Transfer-Encoding") or
		equal_ci(name, "Trailer") or equal_ci(name, "Connection") or
		equal_ci(name, "Host");
}

void append_fields(std::string &result, const fields &values,
	bool skip_framing)
{
	for( const auto &value : values )
	{
		if( skip_framing and framing_header(value.name) ) continue;
		result += value.name;
		result += ": ";
		result += value.value;
		result += "\r\n";
	}
}

void append_trailer_declaration(std::string &result,
	const body_spec::trailer_spec &trailers)
{
	if( trailers.names.empty() ) return;
	result += "Trailer: ";
	for( size_t index = 0; index < trailers.names.size(); ++index )
	{
		if( index != 0 ) result += ", ";
		result += trailers.names[index];
	}
	result += "\r\n";
}

struct parsed_fields
{
	fields values;
	optional<uint64_t> content_length;
	bool chunked = false;
	bool close = false;
};

[[nodiscard]] sys_expected<parsed_fields> parse_fields(
	std::string_view block, const message_limits &limits) noexcept
{
	try {
		parsed_fields result;
		size_t count = 0;
		size_t total = 0;
		while( not block.empty() )
		{
			auto end = block.find("\r\n");
			if( end == std::string_view::npos )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			auto line = block.substr(0, end);
			block.remove_prefix(end + 2);
			if( line.empty() ) break;
			if( ++count > limits.max_field_count or
				total + line.size() > limits.max_header_list_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			total += line.size();
			auto colon = line.find(':');
			if( colon == std::string_view::npos or colon == 0 )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			auto name = line.substr(0, colon);
			auto value = trim(line.substr(colon + 1));
			if( name.size() > limits.max_field_name_size or
				value.size() > limits.max_field_value_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			if( equal_ci(name, "Content-Length") )
			{
				uint64_t length = 0;
				auto parsed = std::from_chars(value.data(),
					value.data() + value.size(), length);
				if( parsed.ec != std::errc{} or
					parsed.ptr != value.data() + value.size() or
					(result.content_length and *result.content_length != length) )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				result.content_length = length;
			}
			else if( equal_ci(name, "Transfer-Encoding") )
			{
				if( not equal_ci(trim(value), "chunked") )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				result.chunked = true;
			}
			else if( equal_ci(name, "Connection") )
				result.close = contains_token(value, "close");
			result.values.add(std::string(name), std::string(value));
		}
		if( result.chunked and result.content_length )
			return sys_unexpected(make_error_code(
				errc::connection_protocol_error));
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace

sys_expected<std::string> serialize_request(const request_head &head,
	const body_spec &spec, protocol version, bool absolute_form) noexcept
{
	try {
		if( version == protocol::v1_0 and
			spec.type() == body_spec::mode::streaming )
			return sys_unexpected(make_error_code(errc::operation_not_supported));
		if( spec.type() == body_spec::mode::head )
			return sys_unexpected(make_error_code(errc::operation_not_supported));
		std::string result;
		result.reserve(256);
		result += head.method.text();
		result += ' ';
		if( absolute_form )
			result += head.url.to_string();
		else
			result += head.target;
		result += ' ';
		result += version_text(version);
		result += "\r\nHost: ";
		result += head.authority;
		result += "\r\n";
		append_fields(result, head.headers, true);
		if( head.upgrade_protocol )
		{
			result += "Connection: Upgrade\r\nUpgrade: ";
			result += *head.upgrade_protocol;
			result += "\r\n";
		}
		if( spec.type() == body_spec::mode::known_length )
		{
			result += "Content-Length: ";
			result += std::to_string(*spec.length());
			result += "\r\n";
		}
		else if( spec.type() == body_spec::mode::streaming )
			result += "Transfer-Encoding: chunked\r\n";
		append_trailer_declaration(result, spec.trailers());
		// HTTP/1.1 is persistent by default. HTTP/1.0 must opt in explicitly;
		// Session capacity decides whether the connection is reusable after
		// the response boundary is known.
		if( version == protocol::v1_0 )
			result += "Connection: keep-alive\r\n";
		result += "\r\n";
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<std::string> serialize_response(const response_head &head,
	const body_spec &spec, protocol version, bool close) noexcept
{
	try {
		if( version == protocol::v1_0 and
			spec.type() == body_spec::mode::streaming )
			return sys_unexpected(make_error_code(errc::operation_not_supported));
		const auto code = head.status.code();
		const bool body_allowed = head.status.allows_body();
		if( not body_allowed and spec.type() != body_spec::mode::empty )
			return sys_unexpected(make_error_code(errc::body_not_allowed));
		std::string result;
		result.reserve(256);
		result += version_text(version);
		result += ' ';
		result += std::to_string(code);
		result += ' ';
		result += head.status.text();
		result += "\r\n";
		append_fields(result, head.headers, true);
		if( head.upgrade_protocol )
		{
			result += "Connection: Upgrade\r\nUpgrade: ";
			result += *head.upgrade_protocol;
			result += "\r\n";
		}
		if( body_allowed and (spec.type() == body_spec::mode::known_length or
			spec.type() == body_spec::mode::head) )
		{
			result += "Content-Length: ";
			result += std::to_string(*spec.length());
			result += "\r\n";
		}
		else if( body_allowed and spec.type() == body_spec::mode::streaming )
			result += "Transfer-Encoding: chunked\r\n";
		append_trailer_declaration(result, spec.trailers());
		if( close ) result += "Connection: close\r\n";
		result += "\r\n";
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<parsed_request> parse_request(std::string_view wire,
	const message_limits &limits) noexcept
{
	try {
		auto line_end = wire.find("\r\n");
		if( line_end == std::string_view::npos or
			line_end > limits.max_start_line_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));
		auto line = wire.substr(0, line_end);
		auto first = line.find(' ');
		auto last = line.rfind(' ');
		if( first == std::string_view::npos or last == first or
			last == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));
		auto parsed_version = parse_version(line.substr(last + 1));
		if( not parsed_version ) return sys_unexpected(parsed_version.error());
		auto parsed = parse_fields(wire.substr(line_end + 2), limits);
		if( not parsed ) return sys_unexpected(parsed.error());
		parsed_request result;
		result.head.method = http_exp::method(std::string(line.substr(0, first)));
		const auto raw_target = line.substr(first + 1, last - first - 1);
		if( raw_target.empty() or
			raw_target.find_first_of(std::string_view(" \t\r\n")) !=
				std::string_view::npos )
			return sys_unexpected(make_error_code(errc::invalid_target));
		result.head.headers = std::move(parsed->values);
		result.head.scheme = "http";
		result.version = *parsed_version;

		size_t host_count = 0;
		std::string_view host_value {};
		for( const auto &field : result.head.headers )
		{
			if( equal_ci(field.name, "Host") )
			{
				++host_count;
				host_value = field.value;
			}
		}
		if( host_count > 1 )
			return sys_unexpected(make_error_code(
				errc::connection_protocol_error));
		if( result.version == protocol::v1_1 and host_count != 1 )
			return sys_unexpected(make_error_code(
				errc::connection_protocol_error));
		if( host_count == 1 and not valid_host_value(host_value) )
			return sys_unexpected(make_error_code(
				errc::invalid_field_value));

		if( raw_target.starts_with("http://") or
			raw_target.starts_with("https://") )
		{
			riwo::url target_url {std::string(raw_target)};
			if( not target_url.is_valid() or target_url.host().empty() or
				target_url.has_fragment() )
				return sys_unexpected(make_error_code(errc::invalid_target));
			result.head.url = target_url;
			result.head.scheme = std::string(target_url.protocol());
			result.head.authority = authority_from_url(target_url);
			std::string origin(target_url.encoded_path());
			if( origin.empty() ) origin = "/";
			if( target_url.has_query() )
				origin += '?' + target_url.encoded_query();
			result.head.target = std::move(origin);
		}
		else
		{
			result.head.target = std::string(raw_target);
			if( host_count == 1 )
				result.head.authority = std::string(host_value);
		}
		result.close = parsed->close or result.version == protocol::v1_0;
		if( parsed->chunked ) result.framing = body_framing::chunked;
		else if( parsed->content_length )
		{
			result.framing = *parsed->content_length == 0 ?
				body_framing::none : body_framing::content_length;
			result.content_length = *parsed->content_length;
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	}
}

sys_expected<parsed_response> parse_response(std::string_view wire,
	const message_limits &limits, bool head_request) noexcept
{
	try {
		auto line_end = wire.find("\r\n");
		if( line_end == std::string_view::npos or
			line_end > limits.max_start_line_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));
		auto line = wire.substr(0, line_end);
		auto first = line.find(' ');
		if( first == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));
		auto second = line.find(' ', first + 1);
		auto parsed_version = parse_version(line.substr(0, first));
		if( not parsed_version ) return sys_unexpected(parsed_version.error());
		auto code_text = line.substr(first + 1,
			(second == std::string_view::npos ? line.size() : second) - first - 1);
		if( code_text.size() != 3 )
			return sys_unexpected(make_error_code(errc::invalid_status));
		uint16_t code = 0;
		auto code_result = std::from_chars(code_text.data(),
			code_text.data() + code_text.size(), code);
		if( code_result.ec != std::errc{} or
			code_result.ptr != code_text.data() + code_text.size() )
			return sys_unexpected(make_error_code(errc::invalid_status));
		auto parsed = parse_fields(wire.substr(line_end + 2), limits);
		if( not parsed ) return sys_unexpected(parsed.error());
		parsed_response result;
		result.head.status = http_exp::status(code);
		result.head.headers = std::move(parsed->values);
		result.version = *parsed_version;
		result.close = parsed->close or result.version == protocol::v1_0;
		if( head_request or not result.head.status.allows_body() )
			result.framing = body_framing::none;
		else if( parsed->chunked ) result.framing = body_framing::chunked;
		else if( parsed->content_length )
		{
			result.framing = *parsed->content_length == 0 ?
				body_framing::none : body_framing::content_length;
			result.content_length = *parsed->content_length;
		}
		else
		{
			result.framing = body_framing::until_eof;
			result.close = true;
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	}
}

sys_expected<uint64_t> parse_chunk_size(std::string_view line) noexcept
{
	auto semicolon = line.find(';');
	line = trim(line.substr(0, semicolon));
	uint64_t result = 0;
	auto parsed = std::from_chars(line.data(), line.data() + line.size(),
		result, 16);
	if( line.empty() or parsed.ec != std::errc{} or
		parsed.ptr != line.data() + line.size() )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	return result;
}

std::string serialize_chunk_header(size_t buffer_size)
{
	char size[2 * sizeof(size_t) + 1] {};
	auto converted = std::to_chars(std::begin(size), std::end(size) - 1,
		buffer_size, 16);
	std::string result(size, converted.ptr);
	result += "\r\n";
	return result;
}

std::string serialize_last_chunk(const fields &trailers)
{
	std::string result = "0\r\n";
	append_fields(result, trailers, false);
	result += "\r\n";
	return result;
}

} //namespace riwo::http_exp::v1::detail
