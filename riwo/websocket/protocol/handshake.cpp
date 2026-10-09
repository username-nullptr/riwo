// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "handshake.h"
#include <riwo/core/algorithm/sha1.h>

namespace riwo::websocket { namespace
{

constexpr std::string_view websocket_guid =
	"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr const char
	* sec_websocket_key        = "Sec-WebSocket-Key",
	* sec_websocket_accept     = "Sec-WebSocket-Accept",
	* sec_websocket_version    = "Sec-WebSocket-Version",
	* sec_websocket_protocol   = "Sec-WebSocket-Protocol",
	* sec_websocket_extensions = "Sec-WebSocket-Extensions";

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool ascii_equal_case_insensitive(std::string_view left, std::string_view right) noexcept
{
	if( left.size() != right.size() )
		return false;

	for(size_t index=0; index<left.size(); index++)
	{
		const auto lower = [](char value) noexcept
		{
			auto byte = static_cast<unsigned char>(value);
			if( byte >= 'A' and byte <= 'Z' )
				byte += 'a' - 'A';
			return byte;
		};
		if( lower(left[index]) != lower(right[index]) )
			return false;
	}
	return true;
}

[[nodiscard]] bool contains_header_token(std::string_view value, std::string_view wanted) noexcept
{
	while( true )
	{
		const auto comma = value.find(',');
		const auto item = trim_ows(value.substr(0, comma));

		if( ascii_equal_case_insensitive(item, wanted) )
			return true;

		if( comma == std::string_view::npos )
			return false;

		value.remove_prefix(comma + 1);
	}
	return false;
}

[[nodiscard]] bool is_token_character(unsigned char value) noexcept
{
	if( (value >= '0' and value <= '9') or
		(value >= 'A' and value <= 'Z') or
		(value >= 'a' and value <= 'z') )
		return true;

	switch(value)
	{
	case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
	case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
		return true;
	default:
		break;
	}
	return false;
}

[[nodiscard]] bool is_token(std::string_view value) noexcept
{
	return not value.empty() and std::ranges::all_of(value, [](char item) {
		return is_token_character(static_cast<unsigned char>(item));
	});
}

class syntax_reader
{
public:
	explicit syntax_reader(std::string_view value) :
		m_value(value) {}

	void skip_ows() noexcept
	{
		while( m_offset < m_value.size() and
			   (m_value[m_offset] == ' ' or m_value[m_offset] == '\t') )
			m_offset++;
	}

	[[nodiscard]] bool empty() const noexcept {
		return m_offset == m_value.size();
	}

	[[nodiscard]] bool consume(char value) noexcept
	{
		if( empty() or m_value[m_offset] != value )
			return false;
		m_offset++;
		return true;
	}

	[[nodiscard]] bool next_is(char value) const noexcept {
		return not empty() and m_value[m_offset] == value;
	}

	[[nodiscard]] bool token(std::string &result)
	{
		const auto begin = m_offset;
		while( m_offset < m_value.size() and
			   is_token_character(static_cast<unsigned char>(m_value[m_offset])) )
			m_offset++;

		if( begin == m_offset )
			return false;

		result.assign(m_value.substr(begin, m_offset - begin));
		return true;
	}

	[[nodiscard]] bool quoted_token(std::string &result)
	{
		if( not consume('"') )
			return false;

		result.clear();
		while( not empty() )
		{
			const auto value = static_cast<unsigned char>(m_value[m_offset++]);
			if( value == '"' )
				return is_token(result);

			if( value == '\\' )
			{
				if( empty() )
					return false;

				const auto escaped = static_cast<unsigned char>(m_value[m_offset++]);
				if( escaped > 0x7F )
					return false;

				result.push_back(static_cast<char>(escaped));
			}
			else
			{
				if( value < 0x20 or value == 0x7F )
					return false;
				result.push_back(static_cast<char>(value));
			}
		}
		return false;
	}

private:
	std::string_view m_value {};
	size_t m_offset = 0;
};

[[nodiscard]] bool parse_token_list
(std::string_view value, std::vector<std::string> &result, bool require_unique)
{
	syntax_reader reader(value);
	reader.skip_ows();

	while( not reader.empty() )
	{
		std::string token;
		if( not reader.token(token) )
			return false;

		if( require_unique and std::ranges::find(result, token) != result.end() )
			return false;

		result.emplace_back(std::move(token));
		reader.skip_ows();

		if( reader.empty() )
			return true;

		if( not reader.consume(',') )
			return false;

		reader.skip_ows();
	}
	return false;
}

[[nodiscard]] bool parse_extension_list(std::string_view value, std::vector<extension> &result)
{
	syntax_reader reader(value);
	reader.skip_ows();

	while( not reader.empty() )
	{
		extension item;
		if( not reader.token(item.name) )
			return false;
		for(;;)
		{
			reader.skip_ows();
			if( not reader.consume(';') )
				break;

			reader.skip_ows();
			extension_parameter parameter;

			if( not reader.token(parameter.name) )
				return false;

			reader.skip_ows();
			if( reader.consume('=') )
			{
				reader.skip_ows();
				std::string parameter_value;

				const auto parsed = reader.next_is('"') ?
					reader.quoted_token(parameter_value) : reader.token(parameter_value);

				if( not parsed )
					return false;

				parameter.value = std::move(parameter_value);
			}
			item.parameters.emplace_back(std::move(parameter));
		}
		result.emplace_back(std::move(item));
		reader.skip_ows();

		if( reader.empty() )
			return true;

		if( not reader.consume(',') )
			return false;

		reader.skip_ows();
	}
	return false;
}

[[nodiscard]] std::string serialize_token_list(const std::vector<std::string> &values)
{
	std::string result;
	for(size_t index=0; index<values.size(); index++)
	{
		if( not is_token(values[index]) )
			return {};

		const auto current = values.begin() +
			static_cast<std::vector<std::string>::difference_type>(index);

		if( std::ranges::find(values.begin(), current, values[index]) != current )
			return {};

		if( index != 0 )
			result += ", ";
		result += values[index];
	}
	return result;
}

[[nodiscard]] std::string serialize_extension_list
(const std::vector<extension> &values)
{
	std::string result;
	for(size_t index=0; index<values.size(); index++)
	{
		const auto &item = values[index];
		if( not is_token(item.name) )
			return {};

		if( index != 0 )
			result += ", ";
		result += item.name;

		for(const auto &parameter : item.parameters)
		{
			if( not is_token(parameter.name) or
				(parameter.value and not is_token(*parameter.value)) )
				return {};

			result += "; ";
			result += parameter.name;

			if( parameter.value )
			{
				result += '=';
				result += *parameter.value;
			}
		}
	}
	return result;
}

[[nodiscard]] int base64_value(char value) noexcept
{
	if( value >= 'A' and value <= 'Z' )
		return value - 'A';
	if( value >= 'a' and value <= 'z' )
		return value - 'a' + 26;
	if( value >= '0' and value <= '9' )
		return value - '0' + 52;
	if( value == '+' )
		return 62;
	if( value == '/' )
		return 63;
	return -1;
}

[[nodiscard]] bool is_client_key(std::string_view value) noexcept
{
	value = trim_ows(value);
	if( value.size() != 24 or value[22] != '=' or value[23] != '=' )
		return false;

	for(size_t index=0; index<22; index++)
	{
		if( base64_value(value[index]) < 0 )
			return false;
	}
	return (base64_value(value[21]) & 0x0F) == 0;
}

[[nodiscard]] std::string base64_encode(std::span<const std::byte> input)
{
	static constexpr std::string_view alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string output;
	output.reserve((input.size() + 2) / 3 * 4);

	for(size_t offset=0; offset<input.size(); offset+=3)
	{
		uint32_t value = static_cast<uint32_t>(std::to_integer<uint8_t>(input[offset])) << 16U;

		if( offset + 1 < input.size() )
			value |= static_cast<uint32_t>(std::to_integer<uint8_t>(input[offset + 1])) << 8U;

		if( offset + 2 < input.size() )
			value |= static_cast<uint32_t>(std::to_integer<uint8_t>(input[offset + 2]));

		output += alphabet[(value >> 18) & 0x3F];
		output += alphabet[(value >> 12) & 0x3F];

		output += offset + 1 < input.size() ? alphabet[(value >> 6) & 0x3F] : '=';
		output += offset + 2 < input.size() ? alphabet[value & 0x3F] : '=';
	}
	return output;
}

[[nodiscard]] const std::string *header_value(const http::headers &headers, const char *name)
{
	const auto it = headers.find(name);
	return it == headers.end() ? nullptr : &it->second.to_string();
}

template <typename Result, typename Function>
[[nodiscard]] sys_expected<Result> guard_allocation(Function &&function) noexcept
{
	error_code error {};
	try {
		return std::forward<Function>(function)();
	}
	catch(const std::bad_alloc&) {
		error = std::make_error_code(std::errc::not_enough_memory);
	}
	catch(...) {
		error = std::make_error_code(std::errc::io_error);
	}
	return sys_unexpected(error);
}

} //namespace

sys_expected<std::string> make_client_key(std::span<const std::byte,16> nonce) noexcept
{
	return guard_allocation<std::string>([&] {
		return base64_encode(nonce);
	});
}

sys_expected<std::string> make_accept_key(std::string_view client_key) noexcept
{
	return guard_allocation<std::string>([&]() -> sys_expected<std::string>
	{
		client_key = trim_ows(client_key);
		if( not is_client_key(client_key) )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		sha1 hash;
		hash.append(client_key).append(websocket_guid).finalize();
		return hash.base64();
	});
}

sys_expected<http::headers> make_opening_request_headers(const opening_request &request) noexcept
{
	return guard_allocation<http::headers>([&]() -> sys_expected<http::headers>
	{
		const auto key = trim_ows(request.key);
		if( not is_client_key(key) )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		const auto protocols = serialize_token_list(request.subprotocols);
		if( not request.subprotocols.empty() and protocols.empty() )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		const auto extensions = serialize_extension_list(request.extensions);
		if( not request.extensions.empty() and extensions.empty() )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		http::headers result {
			{http::header::connection, "Upgrade"},
			{http::header::upgrade, "websocket"},
			{sec_websocket_version, "13"},
			{sec_websocket_key, key},
		};
		if( not protocols.empty() )
			result[sec_websocket_protocol] = protocols;

		if( not extensions.empty() )
			result[sec_websocket_extensions] = extensions;
		return result;
	});
}

sys_expected<opening_request> parse_opening_request
(http::method_enum method, http::version_enum version, const http::headers &headers) noexcept
{
	return guard_allocation<opening_request>([&]() -> sys_expected<opening_request>
	{
		if( method != http::method::get or version != http::version::v11 )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		const auto *connection = header_value(headers, http::header::connection);
		const auto *upgrade = header_value(headers, http::header::upgrade);

		if( not connection or not contains_header_token(*connection, "upgrade") or
			not upgrade or not ascii_equal_case_insensitive(trim_ows(*upgrade), "websocket") )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		const auto *websocket_version = header_value(headers, sec_websocket_version);
		if( not websocket_version or trim_ows(*websocket_version) != "13" )
			return sys_unexpected(make_error_code(errc::unsupported_version));

		const auto *key = header_value(headers, sec_websocket_key);
		if( not key or not is_client_key(*key) )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		opening_request result;
		result.key = trim_ows(*key);

		if( const auto *protocols = header_value(headers, sec_websocket_protocol) )
		{
			if( not parse_token_list(*protocols, result.subprotocols, true) )
				return sys_unexpected(make_error_code(errc::invalid_upgrade));
		}
		if( const auto *extensions = header_value(headers, sec_websocket_extensions) )
		{
			if( not parse_extension_list(*extensions, result.extensions) )
				return sys_unexpected(make_error_code(errc::invalid_upgrade));
		}
		return result;
	});
}

sys_expected<http::headers> make_opening_response_headers
(const opening_request &request, const opening_response &response) noexcept
{
	return guard_allocation<http::headers>([&]() -> sys_expected<http::headers>
	{
		auto accept = make_accept_key(request.key);
		if( not accept )
			return sys_unexpected(accept.error());

		const auto offered_protocols = serialize_token_list(request.subprotocols);
		if( not request.subprotocols.empty() and offered_protocols.empty() )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		if( response.subprotocol )
		{
			if( not is_token(*response.subprotocol) or
				std::ranges::find(request.subprotocols, *response.subprotocol) == request.subprotocols.end() )
				return sys_unexpected(make_error_code(errc::unsupported_subprotocol));
		}
		const auto extensions = serialize_extension_list(response.extensions);
		if( not response.extensions.empty() and extensions.empty() )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		http::headers result {
			{http::header::connection, "Upgrade"},
			{http::header::upgrade, "websocket"},
			{sec_websocket_accept, *accept},
		};
		if( response.subprotocol )
			result[sec_websocket_protocol] = *response.subprotocol;

		if( not extensions.empty() )
			result[sec_websocket_extensions] = extensions;
		return result;
	});
}

sys_expected<opening_response> parse_opening_response(http::status_enum status,
	const http::headers &headers, const opening_request &request) noexcept
{
	return guard_allocation<opening_response>([&]() -> sys_expected<opening_response>
	{
		if( status != http::status::switching_protocols )
			return sys_unexpected(make_error_code(errc::handshake_rejected));

		const auto *connection = header_value(headers, http::header::connection);
		const auto *upgrade = header_value(headers, http::header::upgrade);

		if( not connection or not contains_header_token(*connection, "upgrade") or
			not upgrade or not ascii_equal_case_insensitive(trim_ows(*upgrade), "websocket") )
			return sys_unexpected(make_error_code(errc::invalid_upgrade));

		auto expected_accept = make_accept_key(request.key);
		if( not expected_accept )
			return sys_unexpected(expected_accept.error());

		const auto *accept = header_value(headers, sec_websocket_accept);
		if( not accept or trim_ows(*accept) != *expected_accept )
			return sys_unexpected(make_error_code(errc::invalid_accept_key));

		opening_response result;
		if( const auto *protocol = header_value(headers, sec_websocket_protocol) )
		{
			std::vector<std::string> selected;
			if( not parse_token_list(*protocol, selected, true) or selected.size() != 1 )
				return sys_unexpected(make_error_code(errc::invalid_upgrade));

			if( std::ranges::find(request.subprotocols, selected.front()) == request.subprotocols.end() )
				return sys_unexpected(make_error_code(errc::unsupported_subprotocol));

			result.subprotocol = std::move(selected.front());
		}
		if( const auto *extensions = header_value(headers, sec_websocket_extensions) )
		{
			if( not parse_extension_list(*extensions, result.extensions) )
				return sys_unexpected(make_error_code(errc::invalid_upgrade));
		}
		return result;
	});
}

} //namespace riwo::websocket
