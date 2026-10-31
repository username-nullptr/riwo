// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "wire.h"
#include <riwo/core/utils/string_tools.h>

#include <riwo/experimental/http/protocol/detail/syntax.h>
#include <riwo/experimental/http/protocol/range.h>

namespace riwo::experimental::http::v1::detail { namespace
{

[[nodiscard]] bool equal_ci(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

[[nodiscard]] bool valid_host_value(std::string_view value) noexcept
{
	if( value.empty() )
		return false;

	return value.find(',') == std::string_view::npos and
		std::ranges::none_of(value, [](unsigned char byte)
		{
			return byte <= 0x20U or byte == 0x7fU or byte == '/' or
				   byte == '?' or byte == '#' or byte == '@';
		});
}

[[nodiscard]] bool valid_request_cookie(std::string_view value) noexcept
{
	bool found = false;
	for(;;)
	{
		const auto semicolon = value.find(';');
		const auto item = experimental::http::detail::trim_ows(value.substr(0, semicolon));

		if( not item.empty() )
		{
			found = true;
			if( item.find('=') == std::string_view::npos )
				return false;
		}
		if( semicolon == std::string_view::npos )
			return found;

		value.remove_prefix(semicolon + 1);
	}
}

[[nodiscard]] bool valid_response_cookie(std::string_view value) noexcept
{
	for(;;)
	{
		const auto semicolon = value.find(';');
		const auto item = experimental::http::detail::trim_ows(value.substr(0, semicolon));

		if( not item.empty() )
			return item.find('=') != std::string_view::npos;

		if( semicolon == std::string_view::npos )
			return false;

		value.remove_prefix(semicolon + 1);
	}
}

[[nodiscard]] bool legacy_status_code(uint16_t value) noexcept
{
	switch( value )
	{
	case 100: case 101: case 102:
	case 200: case 201: case 202: case 203: case 204: case 205:
	case 206: case 207: case 208: case 226:
	case 300: case 301: case 302: case 303: case 304: case 305:
	case 307: case 308:
	case 400: case 401: case 402: case 403: case 404: case 405:
	case 406: case 407: case 408: case 409: case 410: case 411:
	case 412: case 413: case 414: case 415: case 416: case 417:
	case 421: case 422: case 423: case 424: case 426: case 428:
	case 429: case 431: case 451:
	case 500: case 501: case 502: case 503: case 504: case 505:
	case 506: case 507: case 508: case 510: case 511:
		return true;

	default: break;
	}
	return false;
}

[[nodiscard]] bool valid_partial_response_fields(uint16_t code, const fields &values) noexcept
{
	size_t content_range_count = 0;
	size_t content_type_count = 0;

	for(const auto &[name, value] : values)
	{
		ignore_unused(value);
		if( equal_ci(name, "Content-Range") )
			++content_range_count;

		else if( equal_ci(name, "Content-Type") )
			++content_type_count;
	}
	if( content_range_count > 1 )
		return false;

	const auto content_range_value = values.find_first("Content-Range");
	if( code == status::range_not_satisfiable )
	{
		if( not content_range_value )
			return true;

		const auto range = parse_content_range(*content_range_value);
		return range and range->unit == "bytes" and not range->satisfied;
	}
	if( code != status::partial_content )
		return true;

	if( content_type_count == 1 )
	{
		if( const auto content_type = values.find_first("Content-Type") )
		{
			if( parse_multipart_byte_ranges_boundary(*content_type) )
				return true;
		}
	}
	if( not content_range_value )
		return false;

	const auto range = parse_content_range(*content_range_value);
	return range and range->unit == "bytes" and range->satisfied;
}

[[nodiscard]] std::string authority_from_url(const url &value)
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

[[nodiscard]] sys_expected<protocol> parse_version(std::string_view value) noexcept
{
	if( value == "HTTP/1.1" )
		return protocol::v1_1;

	if( value == "HTTP/1.0" )
		return protocol::v1_0;

	return sys_unexpected(make_error_code(errc::connection_protocol_error));
}

[[nodiscard]] std::string_view version_text(protocol value) noexcept
{
	return value == protocol::v1_0 ? "HTTP/1.0" : "HTTP/1.1";
}

[[nodiscard]] bool framing_header(std::string_view name) noexcept
{
	return equal_ci(name, "Content-Length") or
		   equal_ci(name, "Transfer-Encoding") or
		   equal_ci(name, "Trailer") or equal_ci(name, "Connection") or
		   equal_ci(name, "Host");
}

void append_fields(std::string &result, const fields &values, bool skip_framing)
{
	for(const auto &[key, value] : values)
	{
		if( skip_framing and framing_header(key) )
			continue;

		result += key;
		result += ": ";
		result += value.to_string();
		result += "\r\n";
	}
}

void append_trailer_declaration(std::string &result, const body_spec::trailer_spec &trailers)
{
	if( trailers.names.empty() )
		return ;

	result += "Trailer: ";
	for(size_t index=0; index<trailers.names.size(); ++index)
	{
		if( index != 0 )
			result += ", ";
		result += trailers.names[index];
	}
	result += "\r\n";
}

struct parsed_fields
{
	fields values {};
	optional<uint64_t> content_length {};

	bool chunked = false;
	bool transfer_encoding = false;
	bool close = false;
};

[[nodiscard]] sys_expected<parsed_fields>
parse_fields(std::string_view block, const message_limits &limits) noexcept
try
{
	parsed_fields result {};
	size_t count = 0;
	size_t total = 0;

	while( not block.empty() )
	{
		auto end = block.find("\r\n");
		if( end == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));

		auto line = block.substr(0, end);
		block.remove_prefix(end + 2);

		if( line.empty() )
			break;

		if( ++count > limits.max_field_count or total + line.size() > limits.max_header_list_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));

		total += line.size();
		auto colon = line.find(':');

		if( colon == std::string_view::npos or colon == 0 )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));

		auto name = line.substr(0, colon);
		auto value = experimental::http::detail::trim_ows(line.substr(colon + 1));

		if( name.size() > limits.max_field_name_size or value.size() > limits.max_field_value_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));

		if( equal_ci(name, "Content-Length") )
		{
			if( result.content_length )
				return sys_unexpected(make_error_code(errc::connection_protocol_error));

			uint64_t length = 0;
			auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);

			if( parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size() )
				return sys_unexpected(make_error_code(errc::connection_protocol_error));

			if( length > std::numeric_limits<size_t>::max() )
				return sys_unexpected(make_error_code(errc::connection_protocol_error));

			result.content_length = length;
		}
		else if( equal_ci(name, "Transfer-Encoding") )
		{
			if( result.transfer_encoding )
				return sys_unexpected(make_error_code(errc::connection_protocol_error));

			if( not equal_ci(experimental::http::detail::trim_ows(value), "chunked") )
				return sys_unexpected(make_error_code(errc::connection_protocol_error));

			result.transfer_encoding = true;
			result.chunked = true;
		}
		else if( equal_ci(name, "Connection") )
			result.close = experimental::http::detail::comma_list_contains(value, "close");

		result.values.add(std::string(name), std::string(value));
	}
	if( result.chunked and result.content_length )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

} //namespace

sys_expected<std::string> serialize_request
(const request_head &head, const body_spec &spec, protocol version, bool absolute_form) noexcept
try
{
	if( version == protocol::v1_0 and spec.type() == body_spec::mode::streaming )
		return sys_unexpected(make_error_code(errc::operation_not_supported));

	if( spec.type() == body_spec::mode::head )
		return sys_unexpected(make_error_code(errc::operation_not_supported));

	std::string result {};
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

sys_expected<std::string> serialize_response
(const response_head &head, const body_spec &spec, protocol version, bool close) noexcept
try
{
	if( version == protocol::v1_0 and spec.type() == body_spec::mode::streaming )
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
	if( body_allowed and
		(spec.type() == body_spec::mode::known_length or spec.type() == body_spec::mode::head) )
	{
		result += "Content-Length: ";
		result += std::to_string(*spec.length());
		result += "\r\n";
	}
	else if( body_allowed and spec.type() == body_spec::mode::streaming )
		result += "Transfer-Encoding: chunked\r\n";

	append_trailer_declaration(result, spec.trailers());
	if( close )
		result += "Connection: close\r\n";

	result += "\r\n";
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

sys_expected<parsed_request>
parse_request(std::string_view wire, const message_limits &limits) noexcept
try
{
	auto line_end = wire.find("\r\n");
	if( line_end == std::string_view::npos or line_end > limits.max_start_line_size )
		return sys_unexpected(make_error_code(errc::header_limit_exceeded));

	auto line = wire.substr(0, line_end);
	auto first = line.find(' ');
	auto last = line.rfind(' ');

	if( first == std::string_view::npos or last == first or last == std::string_view::npos )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));

	auto parsed_version = parse_version(line.substr(last + 1));
	if( not parsed_version )
		return sys_unexpected(parsed_version.error());

	auto parsed = parse_fields(wire.substr(line_end + 2), limits);
	if( not parsed )
		return sys_unexpected(parsed.error());

	parsed_request result {};
	result.head.method = method(line.substr(0, first));

	if( not result.head.method.known() )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));

	const auto raw_target = line.substr(first + 1, last - first - 1);

	if( raw_target.empty() or raw_target.find('#') != std::string_view::npos or
		raw_target.find_first_of(std::string_view(" \t\r\n")) != std::string_view::npos )
		return sys_unexpected(make_error_code(errc::invalid_target));

	result.head.headers = std::move(parsed->values);
	result.head.scheme = "http";
	result.version = *parsed_version;

	size_t host_count = 0;
	std::string_view host_value {};

	for(const auto &[key, value] : result.head.headers)
	{
		if( equal_ci(key, "Host") )
		{
			++host_count;
			host_value = value.to_string();
		}
		if( (equal_ci(key, "Cookie") or equal_ci(key, "Set-Cookie")) and
			not valid_request_cookie(value.to_string()) )
			return sys_unexpected(make_error_code(errc::invalid_field_value));
	}
	if( host_count > 1 )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));

	if( result.version == protocol::v1_1 and host_count != 1 )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));

	if( host_count == 1 and not valid_host_value(host_value) )
		return sys_unexpected(make_error_code(errc::invalid_field_value));

	const auto known_method = *result.head.method.known();
	if( known_method == method::connect )
	{
		if( raw_target.find('/') != std::string_view::npos or
			raw_target.find(':') == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::invalid_target));

		result.head.target = std::string(raw_target);
		result.head.authority = std::string(raw_target);
	}
	else if( raw_target == "*" )
	{
		if( known_method != method::options )
			return sys_unexpected(make_error_code(errc::invalid_target));
		result.head.target = "*";
	}
	else if( raw_target.starts_with("http://") or raw_target.starts_with("https://") )
	{
		url target_url {std::string(raw_target)};
		if( not target_url.is_valid() or target_url.host().empty() or target_url.has_fragment() )
			return sys_unexpected(make_error_code(errc::invalid_target));

		result.head.url = target_url;
		result.head.scheme = std::string(target_url.protocol());
		result.head.authority = authority_from_url(target_url);

		std::string origin(target_url.encoded_path());
		if( origin.empty() )
			origin = "/";

		if( target_url.has_query() )
			origin += '?' + target_url.encoded_query();

		result.head.target = std::move(origin);
	}
	else
	{
		if( not raw_target.starts_with('/') )
			return sys_unexpected(make_error_code(errc::invalid_target));

		result.head.target = std::string(raw_target);
		if( host_count == 1 )
			result.head.authority = std::string(host_value);
	}
	result.close = parsed->close or result.version == protocol::v1_0;
	if( parsed->chunked and result.version != protocol::v1_1 )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));

	if( parsed->chunked )
		result.framing = body_framing::chunked;

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

sys_expected<parsed_response> parse_response
(std::string_view wire, const message_limits &limits, bool head_request) noexcept
{
	try {
		auto line_end = wire.find("\r\n");
		if( line_end == std::string_view::npos or line_end > limits.max_start_line_size )
			return sys_unexpected(make_error_code(errc::header_limit_exceeded));

		auto line = wire.substr(0, line_end);
		auto first = line.find(' ');

		if( first == std::string_view::npos )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));

		auto second = line.find(' ', first + 1);
		auto parsed_version = parse_version(line.substr(0, first));

		if( not parsed_version )
			return sys_unexpected(parsed_version.error());

		auto code_text = line.substr(first + 1,
			(second == std::string_view::npos ? line.size() : second) - first - 1
		);
		if( code_text.size() != 3 )
			return sys_unexpected(make_error_code(errc::invalid_status));

		uint16_t code = 0;
		auto code_result = std::from_chars (
			code_text.data(), code_text.data() + code_text.size(), code
		);
		if( code_result.ec != std::errc{} or code_result.ptr != code_text.data() + code_text.size() )
			return sys_unexpected(make_error_code(errc::invalid_status));

		if( not legacy_status_code(code) )
			return sys_unexpected(make_error_code(errc::invalid_status));

		auto parsed = parse_fields(wire.substr(line_end + 2), limits);
		if( not parsed )
			return sys_unexpected(parsed.error());

		for(const auto &[key, value] : parsed->values)
		{
			if( (equal_ci(key, "Cookie") or equal_ci(key, "Set-Cookie")) and
				not valid_response_cookie(value.to_string()) )
				return sys_unexpected(make_error_code(errc::invalid_field_value));
		}
		if( not valid_partial_response_fields(code, parsed->values) )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));

		if( parsed->chunked and *parsed_version != protocol::v1_1 )
			return sys_unexpected(make_error_code(errc::connection_protocol_error));

		parsed_response result {};
		result.head.status = status(code);
		result.head.headers = std::move(parsed->values);

		result.version = *parsed_version;
		result.close = parsed->close or result.version == protocol::v1_0;

		if( head_request or not result.head.status.allows_body() )
			result.framing = body_framing::none;

		else if( parsed->chunked )
			result.framing = body_framing::chunked;

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
	line = experimental::http::detail::trim_ows(line.substr(0, semicolon));

	uint64_t result = 0;
	auto parsed = std::from_chars(line.data(), line.data() + line.size(), result, 16);

	if( line.empty() or parsed.ec != std::errc{} or parsed.ptr != line.data() + line.size() )
		return sys_unexpected(make_error_code(errc::connection_protocol_error));
	return result;
}

std::string serialize_chunk_header(size_t buffer_size)
{
	char size[2 * sizeof(size_t) + 1] {};
	auto converted = std::to_chars (
		std::begin(size), std::end(size) - 1, buffer_size, 16
	);
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

} //namespace riwo::experimental::http::v1::detail
