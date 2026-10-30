// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp/client.h>
#include "permessage_deflate.h"

namespace riwo::websocket_exp::detail { namespace
{

[[nodiscard]] bool equal_ci(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

[[nodiscard]] bool websocket_owned_request_header(std::string_view name) noexcept
{
	return equal_ci(name, "Host") or equal_ci(name, "Connection") or
		   equal_ci(name, "Upgrade") or equal_ci(name, "Proxy-Authorization") or
		   (name.size() >= 14 and equal_ci(name.substr(0, 14), "Sec-WebSocket-"));
}

[[nodiscard]] sys_expected<url> canonical_websocket_url(const url &endpoint) noexcept
try
{
	if( not endpoint.is_valid() or endpoint.host().empty() or endpoint.has_fragment() )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

	auto scheme = strtls::to_lower(endpoint.protocol());
	if( scheme != "ws" and scheme != "wss" and scheme != "http" and scheme != "https" )
		return sys_unexpected(make_system_error_code(std::errc::protocol_not_supported));

	auto text = endpoint.to_string();
	auto separator = text.find("://");

	if( separator == std::string::npos )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

	text.replace(0, separator,
		scheme == "wss" or scheme == "https" ? "wss" : "ws"
	);
	return url(text);
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

[[nodiscard]] sys_expected<url> websocket_transport_url(const url &endpoint) noexcept
try
{
	auto canonical = canonical_websocket_url(endpoint);
	if( not canonical )
		return sys_unexpected(canonical.error());

	auto text = canonical->to_string();
	auto separator = text.find("://");

	text.replace(0, separator,
		equal_ci(canonical->protocol(), "wss") ? "https" : "http"
	);
	return url(text);
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

[[nodiscard]] uint16_t websocket_effective_port(const url &value) noexcept
{
	if( value.port() != 0 )
		return value.port();
	return equal_ci(value.protocol(), "wss") ? 443 : 80;
}

[[nodiscard]] bool same_websocket_origin(const url &lhs, const url &rhs) noexcept
{
	return equal_ci(lhs.protocol(), rhs.protocol()) and
		   equal_ci(lhs.host(), rhs.host()) and
		   websocket_effective_port(lhs) == websocket_effective_port(rhs);
}

[[nodiscard]] bool websocket_redirect_status(http_exp::status status) noexcept
{
	return status == http_exp::status::moved_permanently or
		   status == http_exp::status::found or
		   status == http_exp::status::see_other or
		   status == http_exp::status::temporary_redirect or
		   status == http_exp::status::permanent_redirect;
}

} //namespace

error_code validate_open_request(connect_request &request, const stream_config &stream) noexcept
try
{
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

	for(const auto &field : request.headers)
	{
		if( websocket_owned_request_header(field.name) )
			return make_error_code(errc::invalid_upgrade);
	}
	return {};
}
catch(...) {
	return exception_error(std::current_exception());
}

bool extended_connect(const http_exp::client_config &config) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	return config.protocols.requirement == http_exp::protocol_requirement::v2;

#else //RIWO_HTTP_EXP_HTTP2_SUPPORT
	ignore_unused(config);
	return false;
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
}

http_exp::client_config websocket_http_config
(http_exp::client_config config, std::chrono::milliseconds handshake_timeout)
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( config.protocols.requirement == http_exp::protocol_requirement::v2 )
		config.h2.enable_extended_connect = true;
	else
		config.protocols.requirement = http_exp::protocol_requirement::v1;

#else ////RIWO_HTTP_EXP_HTTP2_SUPPORT
	config.protocols.requirement = http_exp::protocol_requirement::v1;
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT

	config.redirects.max_redirects = 0;
	config.auto_decompression = false;

	if( handshake_timeout > std::chrono::milliseconds::zero() )
	{
		config.timeout.connect = std::min(config.timeout.connect, handshake_timeout);
		config.timeout.header = std::min(config.timeout.header, handshake_timeout);
		config.timeout.pool_wait = std::min(config.timeout.pool_wait, handshake_timeout);
	}
	return config;
}

prepared_open::prepared_open
(http_exp::request request_value, http_exp::body_spec spec_value, opening_request opening_value) :
	request(std::move(request_value)), spec(std::move(spec_value)), opening(std::move(opening_value))
{

}

sys_expected<prepared_open> prepare_open
(const connect_request &request, const url &endpoint, bool use_extended_connect) noexcept
try
{
	auto target = websocket_transport_url(endpoint);
	if( not target )
		return sys_unexpected(target.error());

	std::array<std::byte,16> nonce {};
	if( auto random = secure_random_bytes(asio::buffer(nonce)); not random )
		return sys_unexpected(random.error());

	auto key = make_client_key(nonce);
	if( not key )
		return sys_unexpected(key.error());

	opening_request opening {
		.key = std::move(*key),
		.subprotocols = request.subprotocols,
		.extensions = request.extensions,
	};
	auto version = http_exp::protocol::v1_1;

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( use_extended_connect )
		version = http_exp::protocol::v2;
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT

	auto handshake = make_opening_request_headers(opening, version);
	if( not handshake )
		return sys_unexpected(handshake.error());

	http_exp::request result(use_extended_connect ?
		http_exp::method::connect : http_exp::method::get, std::move(*target)
	);
	for(const auto &[name, value] : request.headers)
		result.headers().add(name, value);

	for(const auto &[name, value] : *handshake)
	{
		if( not use_extended_connect and
			(equal_ci(name, "Connection") or
			 equal_ci(name, "Upgrade")) )
			continue;
		result.headers().set(name, value);
	}
	if( use_extended_connect )
		result.set_connect_protocol("websocket");
	else
		result.set_upgrade_protocol("websocket");

	auto spec = use_extended_connect ?
		http_exp::body_spec::streaming() : http_exp::body_spec::empty();

	return prepared_open(std::move(result), std::move(spec), std::move(opening));
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

open_response_decision evaluate_open_response(const http_exp::response_head &response,
	const opening_request &opening, const connect_request &request, const url &endpoint,
	http_exp::protocol selected_protocol, size_t redirects) noexcept
{
	open_response_decision result {};
	try {
		if( websocket_redirect_status(response.status) )
		{
			if( redirects >= request.max_redirects )
			{
				result.error = make_error_code(errc::redirect_limit_exceeded);
				return result;
			}
			auto location = response.location();
			if( not location )
			{
				result.error = make_error_code(errc::handshake_rejected);
				return result;
			}
			auto probe = request;
			probe.endpoint = url::resolve(endpoint, *location);

			const auto stream = request.stream_options.value_or(stream_config{});
			if( auto error = validate_open_request(probe, stream) )
			{
				result.error = error;
				return result;
			}
			if( equal_ci(endpoint.protocol(), "wss") and
				equal_ci(probe.endpoint.protocol(), "ws") and
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
		auto accepted = parse_opening_response (
			response.status, response.headers, opening, selected_protocol
		);
		if( not accepted )
		{
			result.error = accepted.error();
			return result;
		}
		if( not supported_extension_response(accepted->extensions, request.extensions) )
		{
			result.error = make_error_code(errc::unsupported_extension);
			return result;
		}
		result.action = open_response_action::accept;
		result.response = std::move(*accepted);
		return result;
	}
	catch(...) {
		result.error = exception_error(std::current_exception());
	}
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

error_code cancellation_error(bool cancelled, const error_code &error) noexcept
{
	return cancelled ? asio::error::make_error_code(asio::error::operation_aborted) : error;
}

} //namespace riwo::websocket_exp::detail
