// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client/detail/proxy.h>
#include <riwo/core/system/app_utls.h>
#include <riwo/core/algorithm/misc.h>

namespace riwo::http::detail { namespace
{

struct RIWO_DECL_HIDDEN proxy_environment
{
	std::string endpoint {};
	optional<std::string> username {};
	optional<std::string> password {};
};

[[nodiscard]] bool ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept
{
	if( lhs.size() != rhs.size() )
		return false;

	for(size_t i = 0; i < lhs.size(); ++i)
	{
		auto left = static_cast<unsigned char>(lhs[i]);
		auto right = static_cast<unsigned char>(rhs[i]);

		if( left >= 'A' and left <= 'Z' )
			left = static_cast<unsigned char>(left + ('a' - 'A'));

		if( right >= 'A' and right <= 'Z' )
			right = static_cast<unsigned char>(right + ('a' - 'A'));

		if( left != right )
			return false;
	}
	return true;
}

[[nodiscard]] optional<std::string> environment_value(std::initializer_list<std::string_view> names)
{
	for(const auto name : names)
	{
		auto value = app::getenv(name);
		if( value )
		{
			auto string = value->string();
			if( not strtls::trimmed(string).empty() )
				return strtls::trimmed(string);
		}
	}
	return nullopt;
}

[[nodiscard]] uint16_t effective_port(const url &target) noexcept
{
	if( target.port() != 0 )
		return target.port();

	if( ascii_equal_case_insensitive(target.protocol(), "http") )
		return 80;

	if( ascii_equal_case_insensitive(target.protocol(), "https") )
		return 443;

	if( ascii_equal_case_insensitive(target.protocol(), "ws") )
		return 80;

	if( ascii_equal_case_insensitive(target.protocol(), "wss") )
		return 443;
	return 0;
}

[[nodiscard]] bool address_in_prefix
(const asio::ip::address &address, const asio::ip::address &network, unsigned prefix) noexcept
{
	if( address.is_v4() != network.is_v4() )
		return false;

	if( address.is_v4() )
	{
		if( prefix > 32 )
			return false;

		const auto address_value = address.to_v4().to_uint();
		const auto network_value = network.to_v4().to_uint();

		const auto mask = prefix == 0 ?
			0U : std::numeric_limits<uint32_t>::max() << (32U - prefix);

		return (address_value & mask) == (network_value & mask);
	}
	if( prefix > 128 )
		return false;

	const auto address_bytes = address.to_v6().to_bytes();
	const auto network_bytes = network.to_v6().to_bytes();

	const auto whole_bytes = prefix / 8;
	const auto remaining_bits = prefix % 8;

	for(unsigned i=0; i<whole_bytes; i++)
	{
		if( address_bytes[i] != network_bytes[i] )
			return false;
	}
	if( remaining_bits == 0 )
		return true;

	const auto mask = static_cast<unsigned char>(0xFFU << (8U - remaining_bits));
	return (address_bytes[whole_bytes] & mask) == (network_bytes[whole_bytes] & mask);
}

[[nodiscard]] bool host_matches(std::string_view target, std::string_view pattern) noexcept
{
	while( pattern.starts_with('.') )
		pattern.remove_prefix(1);

	if( pattern.starts_with("*.") )
		pattern.remove_prefix(2);

	if( pattern.empty() )
		return false;

	if( ascii_equal_case_insensitive(target, pattern) )
		return true;

	return target.size() > pattern.size() and
		   target[target.size() - pattern.size() - 1] == '.' and
		   ascii_equal_case_insensitive(target.substr(target.size() - pattern.size()), pattern);
}

[[nodiscard]] bool no_proxy_entry_matches(const url &target, std::string_view entry) noexcept
{
	auto corr_entry = strtls::trimmed(entry);
	if( corr_entry.empty() )
		return false;

	if( corr_entry == "*" )
		return true;

	std::string_view host = corr_entry;
	optional<uint16_t> port {};

	if( host.starts_with('[') )
	{
		const auto closing = host.find(']');
		if( closing == std::string_view::npos )
			return false;

		if( closing + 1 < host.size() )
		{
			if( host[closing + 1] != ':' )
				return false;

			auto parsed = strtls::to_uint16(host.substr(closing + 2));
			if( not parsed )
				return false;

			port = *parsed;
		}
		host = host.substr(1, closing - 1);
	}
	else if( const auto colon = host.rfind(':');
			 colon != std::string_view::npos and host.find(':') == colon )
	{
		auto parsed = strtls::to_uint16(host.substr(colon + 1));
		if( parsed )
		{
			port = *parsed;
			host = host.substr(0, colon);
		}
	}
	if( port and *port != effective_port(target) )
		return false;

	if( const auto slash = host.rfind('/'); slash != std::string_view::npos )
	{
		auto prefix = strtls::to_uint16(host.substr(slash + 1));
		if( not prefix )
			return false;

		error_code target_error;
		error_code network_error;

		auto target_address = asio::ip::make_address(target.host(), target_error);
		auto network_address = asio::ip::make_address(host.substr(0, slash), network_error);

		return not target_error and not network_error and
			address_in_prefix(target_address, network_address, *prefix);
	}
	return host_matches(target.host(), host);
}

[[nodiscard]] bool bypasses_global_proxy(const url &target)
{
	auto value = environment_value({"no_proxy", "NO_PROXY"});
	if( not value )
		return false;

	std::string_view remaining(*value);
	while( not remaining.empty() )
	{
		const auto comma = remaining.find(',');
		const auto entry = remaining.substr(0, comma);

		if( no_proxy_entry_matches(target, entry) )
			return true;

		if( comma == std::string_view::npos )
			break;

		remaining.remove_prefix(comma + 1);
	}
	return false;
}

[[nodiscard]] sys_expected<proxy_environment>
parse_proxy_environment(std::string text) noexcept
{
	try {
		text = strtls::trimmed(text);
		if( text.find("://") == std::string::npos )
			text.insert(0, "http://");

		const auto scheme_end = text.find("://");
		const auto authority_begin = scheme_end + 3;

		const auto authority_end = text.find_first_of("/?#", authority_begin);
		const auto at = text.rfind('@', authority_end);

		proxy_environment result;
		if( at != std::string::npos and at >= authority_begin )
		{
			const std::string_view user_info (
				text.data() + authority_begin, at - authority_begin
			);
			const auto colon = user_info.find(':');

			result.username = from_percent_encoding(user_info.substr(0, colon));
			result.password = colon == std::string_view::npos ?
				std::string{} : from_percent_encoding(user_info.substr(colon + 1));

			text.erase(authority_begin, at - authority_begin + 1);
		}
		result.endpoint = std::move(text);
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
}

[[nodiscard]] std::string base64_encode(std::string_view input)
{
	static constexpr std::string_view alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string output	;
	output.reserve((input.size() + 2) / 3 * 4);

	for(size_t offset = 0; offset < input.size(); offset += 3)
	{
		uint32_t value = static_cast<uint32_t>(static_cast<unsigned char>(input[offset])) << 16U;

		if( offset + 1 < input.size() )
			value |= static_cast<uint32_t>(static_cast<unsigned char>(input[offset + 1])) << 8U;

		if( offset + 2 < input.size() )
			value |= static_cast<uint32_t>(static_cast<unsigned char>(input[offset + 2]));

		output += alphabet[(value >> 18) & 0x3F];
		output += alphabet[(value >> 12) & 0x3F];

		output += offset + 1 < input.size() ? alphabet[(value >> 6) & 0x3F] : '=';
		output += offset + 2 < input.size() ? alphabet[value & 0x3F] : '=';
	}
	return output;
}

[[nodiscard]] sys_expected<std::string> basic_authorization
(std::string_view username, std::string_view password) noexcept
{
	try {
		if( username.find(':') != std::string_view::npos )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		return "Basic " + base64_encode (
			std::string(username) + ":" + std::string(password)
		);
	}
	catch(const std::bad_alloc&) {}
	return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
}

[[nodiscard]] sys_expected<resolved_proxy> resolve_environment_proxy
(const url &target, std::initializer_list<std::string_view> variable_names) noexcept
{
	try {
		resolved_proxy result {};
		if( bypasses_global_proxy(target) )
			return result;

		auto value = environment_value(variable_names);
		if( not value )
			return result;

		auto parsed = parse_proxy_environment(std::move(*value));
		if( not parsed )
			return sys_unexpected(parsed.error());

		url endpoint(parsed->endpoint);
		if( not endpoint.is_valid() or endpoint.host().empty() or
			endpoint.has_fragment() or endpoint.has_query() or endpoint.path() != "/" )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		const auto scheme = strtls::to_lower(endpoint.protocol());
		const bool secure_target =
			ascii_equal_case_insensitive(target.protocol(), "https") or
			ascii_equal_case_insensitive(target.protocol(), "wss");

		if( scheme == "http" or scheme == "https" )
		{
			if( parsed->username )
			{
				auto authorization = basic_authorization(*parsed->username,
					parsed->password.value_or(""));

				if( not authorization )
					return sys_unexpected(authorization.error());

				result.authorization = std::move(*authorization);
			}
			if( not secure_target )
			{
				result.forward = std::move(endpoint);
				return result;
			}
			result.tunnel = proxy_tunnel
			{
				.type = proxy_tunnel_type::http_connect,
				.host = std::string(endpoint.host()),

				.port = endpoint.port() == 0 ?
					static_cast<uint16_t>(scheme == "https" ? 443 : 80) : endpoint.port(),

				.security = scheme == "https" ? security_mode::tls : security_mode::plain,
				.authorization = result.authorization,
			};
			result.authorization.reset();
			return result;
		}
		if( scheme == "socks5" or scheme == "socks5h" )
		{
			result.tunnel = proxy_tunnel
			{
				.type = proxy_tunnel_type::socks5,
				.host = std::string(endpoint.host()),

				.port = endpoint.port() == 0 ?
					static_cast<uint16_t>(1080) : endpoint.port(),

				.security = security_mode::plain,
				.username = std::move(parsed->username),
				.password = std::move(parsed->password),
			};
			return result;
		}
		return sys_unexpected(make_system_error_code(std::errc::protocol_not_supported));
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

} //namespace

sys_expected<resolved_proxy> resolve_global_proxy
(const url &target, std::initializer_list<std::string_view> variable_names) noexcept
{
	return resolve_environment_proxy(target, variable_names);
}

sys_expected<resolved_proxy> resolve_proxy(const url &target, const proxy_t &setting) noexcept
{
	try {
		resolved_proxy result {};
		if( std::holds_alternative<no_proxy_t>(setting) )
			return result;

		if( std::holds_alternative<use_global_proxy_t>(setting) )
		{
			if( ascii_equal_case_insensitive(target.protocol(), "http") )
			{
				return resolve_global_proxy(target, {
					"http_proxy", "HTTP_PROXY", "all_proxy", "ALL_PROXY"
				});
			}
			if( ascii_equal_case_insensitive(target.protocol(), "https") )
			{
				return resolve_global_proxy(target, {
					"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"
				});
			}
			return resolve_global_proxy(target, {"all_proxy", "ALL_PROXY"});
		}
		if( const auto *forward = std::get_if<url>(&setting) )
		{
			if( not forward->is_valid() or forward->host().empty() or
				forward->has_fragment() or forward->has_query() or forward->path() != "/" )
				return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

			const auto scheme = strtls::to_lower(forward->protocol());
			if( scheme != "http" and scheme != "https" )
				return sys_unexpected(make_system_error_code(std::errc::protocol_not_supported));

			result.forward = *forward;
			return result;
		}
		result.tunnel = std::get<proxy_tunnel>(setting);
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

} //namespace riwo::http::detail
