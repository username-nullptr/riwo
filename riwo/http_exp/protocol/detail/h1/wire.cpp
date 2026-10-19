// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "wire.h"
#include <charconv>

namespace riwo::http_exp::detail::h1 { namespace
{

[[nodiscard]] bool equal_ci(std::string_view lhs,
	std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() &&
		std::ranges::equal(lhs, rhs, [](unsigned char left, unsigned char right) {
			return std::tolower(left) == std::tolower(right);
		});
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
	while( !value.empty() && (value.front() == ' ' || value.front() == '\t') )
		value.remove_prefix(1);
	while( !value.empty() && (value.back() == ' ' || value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool contains_token(std::string_view value,
	std::string_view token) noexcept
{
	while( !value.empty() )
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
		return byte <= 0x20U || byte == 0x7fU || byte == '/' ||
			byte == '?' || byte == '#' || byte == '@';
	});
}

[[nodiscard]] std::string authority_from_url(const riwo::url &value)
{
	auto host = std::string(value.host());
	if( host.find(':') != std::string::npos && !host.starts_with('[') )
		host = '[' + host + ']';
	const auto scheme = std::string_view(value.protocol());
	const auto default_port = scheme == "https" ? 443 : 80;
	if( value.port() != 0 && value.port() != default_port )
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

[[nodiscard]] std::string_view reason(uint16_t code) noexcept
{
	switch(code)
	{
	case 100: return "Continue";
	case 101: return "Switching Protocols";
	case 200: return "OK";
	case 201: return "Created";
	case 202: return "Accepted";
	case 204: return "No Content";
	case 301: return "Moved Permanently";
	case 302: return "Found";
	case 304: return "Not Modified";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 500: return "Internal Server Error";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable";
	default: return "Status";
	}
}

[[nodiscard]] bool framing_header(std::string_view name) noexcept {
	return equal_ci(name, "Content-Length") ||
		equal_ci(name, "Transfer-Encoding") ||
		equal_ci(name, "Trailer") || equal_ci(name, "Connection") ||
		equal_ci(name, "Host");
}

void append_fields(std::string &result, const fields &values,
	bool skip_framing)
{
	for( const auto &value : values )
	{
		if( skip_framing && framing_header(value.name) ) continue;
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
		while( !block.empty() )
		{
			auto end = block.find("\r\n");
			if( end == std::string_view::npos )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			auto line = block.substr(0, end);
			block.remove_prefix(end + 2);
			if( line.empty() ) break;
			if( ++count > limits.max_field_count ||
				total + line.size() > limits.max_header_list_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			total += line.size();
			auto colon = line.find(':');
			if( colon == std::string_view::npos || colon == 0 )
				return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			auto name = line.substr(0, colon);
			auto value = trim(line.substr(colon + 1));
			if( name.size() > limits.max_field_name_size ||
				value.size() > limits.max_field_value_size )
				return sys_unexpected(make_error_code(errc::header_limit_exceeded));
			if( equal_ci(name, "Content-Length") )
			{
				uint64_t length = 0;
				auto parsed = std::from_chars(value.data(),
					value.data() + value.size(), length);
				if( parsed.ec != std::errc{} ||
					parsed.ptr != value.data() + value.size() ||
					(result.content_length && *result.content_length != length) )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				result.content_length = length;
			}
			else if( equal_ci(name, "Transfer-Encoding") )
			{
				if( !equal_ci(trim(value), "chunked") )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				result.chunked = true;
			}
			else if( equal_ci(name, "Connection") )
				result.close = contains_token(value, "close");
			result.values.add(std::string(name), std::string(value));
		}
		if( result.chunked && result.content_length )
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
		if( version == protocol::v1_0 &&
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
		if( version == protocol::v1_0 &&
			spec.type() == body_spec::mode::streaming )
			return sys_unexpected(make_error_code(errc::operation_not_supported));
		const auto code = head.status.code();
		const bool body_allowed = code >= 200 && code != 204 && code != 304;
		if( !body_allowed && spec.type() != body_spec::mode::empty )
			return sys_unexpected(make_error_code(errc::body_not_allowed));
		std::string result;
		result.reserve(256);
		result += version_text(version);
		result += ' ';
		result += std::to_string(code);
		result += ' ';
		result += reason(code);
		result += "\r\n";
		append_fields(result, head.headers, true);
		if( body_allowed && (spec.type() == body_spec::mode::known_length ||
			spec.type() == body_spec::mode::head) )
		{
			result += "Content-Length: ";
			result += std::to_string(*spec.length());
			result += "\r\n";
		}
		else if( body_allowed && spec.type() == body_spec::mode::streaming )
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
		if( line_end == std::string_view::npos ||
			line_end > limits.max_start_line_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));
		auto line = wire.substr(0, line_end);
		auto first = line.find(' ');
		auto last = line.rfind(' ');
		if( first == std::string_view::npos || last == first ||
			last == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));
		auto parsed_version = parse_version(line.substr(last + 1));
		if( !parsed_version ) return sys_unexpected(parsed_version.error());
		auto parsed = parse_fields(wire.substr(line_end + 2), limits);
		if( !parsed ) return sys_unexpected(parsed.error());
		parsed_request result;
		result.head.method = http_exp::method(std::string(line.substr(0, first)));
		const auto raw_target = line.substr(first + 1, last - first - 1);
		if( raw_target.empty() ||
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
		if( result.version == protocol::v1_1 && host_count != 1 )
			return sys_unexpected(make_error_code(
				errc::connection_protocol_error));
		if( host_count == 1 && !valid_host_value(host_value) )
			return sys_unexpected(make_error_code(
				errc::invalid_field_value));

		if( raw_target.starts_with("http://") ||
			raw_target.starts_with("https://") )
		{
			riwo::url target_url {std::string(raw_target)};
			if( !target_url.is_valid() || target_url.host().empty() ||
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
		result.close = parsed->close || result.version == protocol::v1_0;
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
		if( line_end == std::string_view::npos ||
			line_end > limits.max_start_line_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));
		auto line = wire.substr(0, line_end);
		auto first = line.find(' ');
		if( first == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));
		auto second = line.find(' ', first + 1);
		auto parsed_version = parse_version(line.substr(0, first));
		if( !parsed_version ) return sys_unexpected(parsed_version.error());
		auto code_text = line.substr(first + 1,
			(second == std::string_view::npos ? line.size() : second) - first - 1);
		if( code_text.size() != 3 )
			return sys_unexpected(make_error_code(errc::invalid_status));
		uint16_t code = 0;
		auto code_result = std::from_chars(code_text.data(),
			code_text.data() + code_text.size(), code);
		if( code_result.ec != std::errc{} ||
			code_result.ptr != code_text.data() + code_text.size() )
			return sys_unexpected(make_error_code(errc::invalid_status));
		auto parsed = parse_fields(wire.substr(line_end + 2), limits);
		if( !parsed ) return sys_unexpected(parsed.error());
		parsed_response result;
		result.head.status = http_exp::status(code);
		result.head.headers = std::move(parsed->values);
		result.version = *parsed_version;
		result.close = parsed->close || result.version == protocol::v1_0;
		if( head_request || code < 200 || code == 204 || code == 304 )
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
	if( line.empty() || parsed.ec != std::errc{} ||
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

} //namespace riwo::http_exp::detail::h1
