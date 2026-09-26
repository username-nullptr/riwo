// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/client.h>

namespace riwo::websocket
{

proxy_config &proxy_config::set_basic_auth(std::string_view user, std::string_view secret)
{
	if( type == proxy_type::socks5 )
	{
		username = user;
		password = secret;
		authorization.reset();
		return *this;
	}
	http::request_arg options;
	options.set_proxy_basic_auth(user, secret);

	auto value = options.headers().find(http::header::proxy_authorization);
	if( value == options.headers().end() )
		throw std::runtime_error("Unable to create proxy credentials");

	authorization = value->second.to_string();
	username = user;
	password = secret;
	return *this;
}

proxy_config &proxy_config::set_bearer_auth(std::string_view token)
{
	if( type == proxy_type::socks5 )
		throw std::invalid_argument("SOCKS5 does not support Bearer authentication");

	authorization = "Bearer " + std::string(token);
	username.reset();
	password.reset();
	return *this;
}

namespace detail
{

bool ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept
{
	if( lhs.size() != rhs.size() )
		return false;

	for(size_t index=0; index<lhs.size(); ++index)
	{
		auto left = static_cast<unsigned char>(lhs[index]);
		auto right = static_cast<unsigned char>(rhs[index]);

		if( left >= 'A' and left <= 'Z' )
			left = static_cast<unsigned char>(left + ('a' - 'A'));

		if( right >= 'A' and right <= 'Z' )
			right = static_cast<unsigned char>(right + ('a' - 'A'));

		if( left != right )
			return false;
	}
	return true;
}

bool websocket_owned_request_header(std::string_view name) noexcept
{
	return ascii_equal_case_insensitive(name, http::header::host) or
		   ascii_equal_case_insensitive(name, http::header::connection) or
		   ascii_equal_case_insensitive(name, http::header::upgrade) or
		   ascii_equal_case_insensitive(name, http::header::proxy_authorization) or
		   (name.size() >= 14 and ascii_equal_case_insensitive(name.substr(0, 14), "Sec-WebSocket-"));
}

sys_expected<url> canonical_websocket_url(const url &endpoint) noexcept
{
	try {
		if( not endpoint.is_valid() or endpoint.host().empty() or endpoint.has_fragment() )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		const auto scheme = strtls::to_lower(endpoint.protocol());
		std::string_view canonical_scheme;

		if( scheme == "ws" or scheme == "http" )
			canonical_scheme = "ws";

		else if( scheme == "wss" or scheme == "https" )
			canonical_scheme = "wss";
		else
		{
			return sys_unexpected (
				make_system_error_code(std::errc::protocol_not_supported)
			);
		}
		auto text = endpoint.to_string();
		auto separator = text.find("://");

		if( separator == std::string::npos )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		text.replace(0, separator, canonical_scheme);
		url result(text);

		if( not result.is_valid() )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

error_code validate_open_request(connect_request &request, const stream_config &stream) noexcept
{
	try {
		auto endpoint = canonical_websocket_url(request.endpoint);
		if( not endpoint )
			return endpoint.error();

		request.endpoint = std::move(*endpoint);
		if( stream.read_buffer_size == 0 or
			stream.ping_interval < std::chrono::milliseconds::zero() or
			stream.compression.level < -1 or stream.compression.level > 9 )
			return make_system_error_code(std::errc::invalid_argument);

		if( not supported_extension_offers(request.extensions) )
			return make_error_code(errc::unsupported_extension);

		if( request.proxy )
		{
			if( const auto *proxy = std::get_if<proxy_config>(&*request.proxy) )
			{
				const auto proxy_scheme = strtls::to_lower(proxy->endpoint.protocol());

				if( not proxy->endpoint.is_valid() or proxy->endpoint.host().empty() or
					proxy->endpoint.has_fragment() or proxy->endpoint.has_query() or
					(not proxy->endpoint.path().empty() and proxy->endpoint.path() != "/") )
					return make_system_error_code(std::errc::invalid_argument);

				if( proxy->type == proxy_type::http )
				{
					if( proxy_scheme != "http" and proxy_scheme != "https" )
						return make_system_error_code(std::errc::protocol_not_supported);

					if( request.endpoint.protocol() == "wss" and proxy_scheme == "https" )
						return make_system_error_code(std::errc::operation_not_supported);
				}
				else if( proxy->type == proxy_type::socks5 )
				{
					if( proxy_scheme != "socks5" and proxy_scheme != "socks5h" )
						return make_system_error_code(std::errc::protocol_not_supported);

					if( request.endpoint.host().size() > 255 or
						(proxy->username and proxy->username->size() > 255) or
						(proxy->password and proxy->password->size() > 255) )
						return make_system_error_code(std::errc::invalid_argument);
				}
				else
					return make_system_error_code(std::errc::invalid_argument);

				if( proxy->authorization and
					(proxy->authorization->find('\r') != std::string::npos or
					 proxy->authorization->find('\n') != std::string::npos) )
					return make_system_error_code(std::errc::invalid_argument);
			}
		}
		for(const auto &[name, value] : request.request_options.headers())
		{
			ignore_unused(value);
			if( websocket_owned_request_header(name) )
				return make_error_code(errc::invalid_upgrade);
		}
		return {};
	}
	catch(const std::bad_alloc&) {
		return make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {}
	return make_system_error_code(std::errc::io_error);
}

sys_expected<url> http_transport_url(const url &endpoint) noexcept
{
	try {
		auto text = endpoint.to_string();
		auto separator = text.find("://");

		if( separator == std::string::npos )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		const auto scheme = strtls::to_lower(endpoint.protocol());
		text.replace(0, separator, scheme == "wss" ? "https" : "http");

		url result(text);
		if( not result.is_valid() )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

uint16_t websocket_effective_port(const url &value) noexcept
{
	if( value.port() != 0 )
		return value.port();
	return ascii_equal_case_insensitive(value.protocol(), "wss") ? 443 : 80;
}

bool same_websocket_origin(const url &lhs, const url &rhs) noexcept
{
	return ascii_equal_case_insensitive(lhs.protocol(), rhs.protocol()) and
		   ascii_equal_case_insensitive(lhs.host(), rhs.host()) and
		   websocket_effective_port(lhs) == websocket_effective_port(rhs);
}

bool websocket_redirect_status(http::status_enum status) noexcept
{
	return status == http::status::moved_permanently or
		   status == http::status::found or
		   status == http::status::see_other or
		   status == http::status::temporary_redirect or
		   status == http::status::permanent_redirect;
}

std::vector<std::byte> pending_bytes(const std::string &pending)
{
	std::vector<std::byte> result(pending.size());
	if( not pending.empty() )
		std::memcpy(result.data(), pending.data(), pending.size());
	return result;
}

std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point deadline) noexcept
{
	const auto now = std::chrono::steady_clock::now();
	if( now >= deadline )
		return std::chrono::milliseconds::zero();

	auto remaining = std::chrono::duration_cast
		<std::chrono::milliseconds>(deadline - now);

	return remaining > std::chrono::milliseconds::zero() ?
		remaining : std::chrono::milliseconds(1);
}

sys_expected<open_http_request> prepare_open_http_request(const connect_request &request,
	const url &endpoint, const http::request_arg &base_options, bool inherit_global_proxy) noexcept
{
	try {
		std::array<std::byte,16> nonce {};
		if( auto random = secure_random_bytes(asio::buffer(nonce)); not random )
			return sys_unexpected(random.error());

		auto key = make_client_key(nonce);
		if( not key )
			return sys_unexpected(key.error());

		open_http_request result;
		result.opening = opening_request {
			.key = std::move(*key),
			.subprotocols = request.subprotocols,
			.extensions = request.extensions,
		};
		auto opening_headers = make_opening_request_headers(result.opening);
		if( not opening_headers )
			return sys_unexpected(opening_headers.error());

		auto transport = http_transport_url(endpoint);
		if( not transport )
			return sys_unexpected(transport.error());

		result.transport = std::move(*transport);
		result.options = base_options;

		for(auto &[name, value] : *opening_headers)
			result.options.set_header(name, value);

		const bool use_global = request.proxy ?
			std::holds_alternative<use_global_proxy_t>(*request.proxy) :
			inherit_global_proxy;

		if( not request.proxy and not use_global )
			return result;

		if( use_global )
		{
			auto resolved = [&]() -> sys_expected<http::detail::resolved_proxy>
			{
				if( ascii_equal_case_insensitive(endpoint.protocol(), "ws") )
				{
					return http::detail::resolve_global_proxy(endpoint, {
						"ws_proxy", "WS_PROXY", "http_proxy", "HTTP_PROXY",
						"all_proxy", "ALL_PROXY"
					});
				}
				if( ascii_equal_case_insensitive(endpoint.protocol(), "wss") )
				{
					return http::detail::resolve_global_proxy(endpoint, {
						"wss_proxy", "WSS_PROXY", "https_proxy", "HTTPS_PROXY",
						"all_proxy", "ALL_PROXY"
					});
				}
				return sys_unexpected (
					make_system_error_code(std::errc::protocol_not_supported)
				);
			}();
			if( not resolved )
				return sys_unexpected(resolved.error());

			if( resolved->forward )
			{
				result.proxy = std::move(*resolved->forward);
				if( resolved->authorization and
					result.options.headers().find(http::header::proxy_authorization) ==
					result.options.headers().end() )
				{
					result.options.set_header(http::header::proxy_authorization,
						*resolved->authorization);
				}
			}
			else if( resolved->tunnel )
				result.proxy = std::move(*resolved->tunnel);
			else
				result.proxy = no_proxy;
			return result;
		}
		if( std::holds_alternative<no_proxy_t>(*request.proxy) )
		{
			result.proxy = no_proxy;
			return result;
		}
		const auto &config = std::get<proxy_config>(*request.proxy);
		const auto endpoint_scheme = strtls::to_lower(endpoint.protocol());
		const auto proxy_scheme = strtls::to_lower(config.endpoint.protocol());

		if( config.type == proxy_type::http and endpoint_scheme == "ws" )
		{
			if( config.authorization )
				result.options.set_header(http::header::proxy_authorization, *config.authorization);

			result.proxy = config.endpoint;
			return result;
		}
		http::proxy_tunnel tunnel;
		tunnel.type = config.type == proxy_type::http ?
			http::proxy_tunnel_type::http_connect :
			http::proxy_tunnel_type::socks5;

		tunnel.host = config.endpoint.host();
		tunnel.port = config.endpoint.port();

		if( tunnel.port == 0 )
		{
			tunnel.port = config.type == proxy_type::socks5 ? 1080 :
				proxy_scheme == "https" ? 443 : 80;
		}
		tunnel.security = proxy_scheme == "https" ?
			http::security_mode::tls : http::security_mode::plain;

		tunnel.authorization = config.authorization;
		tunnel.username = config.username;
		tunnel.password = config.password;
		result.proxy = std::move(tunnel);
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

open_response_decision evaluate_open_response(http::status_enum status, const http::headers &headers,
	const opening_request &opening, const connect_request &request, const url &endpoint,
	const stream_config &stream, size_t redirects) noexcept
{
	open_response_decision result;
	try {
		if( websocket_redirect_status(status) )
		{
			if( redirects >= request.max_redirects )
			{
				result.error = make_error_code(errc::redirect_limit_exceeded);
				result.record_diagnostics = true;
				return result;
			}
			auto location = headers.find(http::header::location);
			if( location == headers.end() )
			{
				result.error = make_error_code(errc::handshake_rejected);
				result.record_diagnostics = true;
				return result;
			}
			auto probe = request;
			probe.endpoint = url::resolve(endpoint, location->second.to_string());

			if( auto error = validate_open_request(probe, stream) )
			{
				result.error = error;
				return result;
			}
			if( ascii_equal_case_insensitive(endpoint.protocol(), "wss") and
				ascii_equal_case_insensitive(probe.endpoint.protocol(), "ws") and
				not request.allow_insecure_redirects )
			{
				result.error = make_error_code(errc::insecure_redirect);
				return result;
			}
			result.action = open_response_action::redirect;
			result.clear_credentials = not same_websocket_origin(endpoint, probe.endpoint);
			result.redirect_endpoint = std::move(probe.endpoint);
			return result;
		}
		result.record_diagnostics = true;
		auto response = parse_opening_response(status, headers, opening);
		if( not response )
		{
			result.error = response.error();
			result.close_connection = status == http::status::switching_protocols;
			return result;
		}
		if( not supported_extension_response(response->extensions, request.extensions) )
		{
			result.error = make_error_code(errc::unsupported_extension);
			result.close_connection = true;
			return result;
		}
		result.action = open_response_action::accept;
		result.response = std::move(*response);
		return result;
	}
	catch(...) {
		result.error = exception_error(std::current_exception());
	}
	return result;
}

}} //namespace riwo::websocket::detail
