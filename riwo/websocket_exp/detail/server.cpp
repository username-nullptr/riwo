// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp/server.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::websocket_exp::detail
{

bool server_equal_ci(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

bool server_is_h2(http_exp::protocol version) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	return version == http_exp::protocol::v2;
#else
	ignore_unused(version);
	return false;
#endif
}

bool server_deadline_expired(
	std::chrono::steady_clock::time_point deadline) noexcept {
	return std::chrono::steady_clock::now() >= deadline;
}

bool valid_rejection_status(http_exp::status status) noexcept
{
	const auto value = status.code();
	return value >= 200 and value <= 599 and
		status != http_exp::status::switching_protocols;
}

void erase_protocol_response_headers(http_exp::fields &headers) noexcept
{
	headers.erase("Connection");
	headers.erase("Upgrade");
	headers.erase("Sec-WebSocket-Accept");
	headers.erase("Sec-WebSocket-Version");
	headers.erase("Sec-WebSocket-Protocol");
	headers.erase("Sec-WebSocket-Extensions");
	headers.erase("Content-Length");
	headers.erase("Transfer-Encoding");
}

void reject_upgrade(server_upgrade_plan &plan, error_code error,
	http_exp::status status)
{
	plan.accepted = false;
	plan.error = error;
	plan.response = {};
	plan.response.status = status;
	plan.body.clear();
	if( status == http_exp::status::upgrade_required )
		plan.response.headers.set("Sec-WebSocket-Version", "13");
}

void reject_upgrade(server_upgrade_plan &plan, upgrade_rejection rejection,
	error_code error)
{
	plan.accepted = false;
	plan.error = error;
	plan.response = {};
	plan.response.status = valid_rejection_status(rejection.status) ?
		rejection.status : http_exp::status{
			http_exp::status::internal_server_error};
	plan.response.headers = std::move(rejection.headers);
	erase_protocol_response_headers(plan.response.headers);
	plan.body = std::move(rejection.body);
}

void reject_selector_exception(server_upgrade_plan &plan,
	const std::exception_ptr &exception) noexcept
{
	reject_upgrade(plan, exception_error(exception),
		http_exp::status::internal_server_error);
	plan.preserve_error_on_write_failure = true;
}

void select_server_subprotocol(server_upgrade_plan &plan,
	const upgrade_options &options) noexcept
{
	try {
		if( options.subprotocol_selector )
			plan.response_message.subprotocol = options.subprotocol_selector(
				plan.request, plan.opening.subprotocols);
		else
			for( const auto &offered : plan.opening.subprotocols )
				if( std::ranges::find(options.supported_subprotocols, offered) !=
					options.supported_subprotocols.end() )
				{
					plan.response_message.subprotocol = offered;
					break;
				}
	}
	catch(...) { reject_selector_exception(plan, std::current_exception()); }
}

void validate_server_subprotocol(server_upgrade_plan &plan,
	const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted ) return;
	if( server_deadline_expired(deadline) )
		return reject_upgrade(plan,
			asio::error::make_error_code(asio::error::timed_out));
	if( plan.response_message.subprotocol and
		(std::ranges::find(plan.opening.subprotocols,
			*plan.response_message.subprotocol) ==
			plan.opening.subprotocols.end() or
		 std::ranges::find(options.supported_subprotocols,
			*plan.response_message.subprotocol) ==
			options.supported_subprotocols.end()) )
		return reject_upgrade(plan,
			make_error_code(errc::unsupported_subprotocol));
	if( options.require_subprotocol and
		not plan.response_message.subprotocol )
		reject_upgrade(plan, make_error_code(errc::unsupported_subprotocol));
}

void select_server_extensions(server_upgrade_plan &plan,
	const upgrade_options &options) noexcept
{
	try {
		if( options.extension_selector )
			plan.response_message.extensions = options.extension_selector(
				plan.request, plan.opening.extensions);
		else if( not options.supported_extensions.empty() )
			for( const auto &offered : plan.opening.extensions )
			{
				for( const auto &policy : options.supported_extensions )
					if( auto selected = negotiate_permessage_deflate(
						offered, policy) )
					{
						plan.response_message.extensions = {
							std::move(*selected)};
						break;
					}
				if( not plan.response_message.extensions.empty() ) break;
			}
	}
	catch(...) { reject_selector_exception(plan, std::current_exception()); }
}

void validate_server_extensions(server_upgrade_plan &plan,
	const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted ) return;
	if( not plan.response_message.extensions.empty() and
		(not supported_extension_response(plan.response_message.extensions,
			plan.opening.extensions) or options.supported_extensions.empty()) )
		return reject_upgrade(plan,
			make_error_code(errc::unsupported_extension));
	if( server_deadline_expired(deadline) )
		reject_upgrade(plan,
			asio::error::make_error_code(asio::error::timed_out));
}

void finalize_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted ) return;
	try {
		if( server_deadline_expired(deadline) )
			return reject_upgrade(plan,
				asio::error::make_error_code(asio::error::timed_out));
		auto headers = make_opening_response_headers(plan.opening,
			plan.response_message, plan.request.version);
		if( not headers ) return reject_upgrade(plan, headers.error());
		plan.response = {};
		plan.response.headers = options.response_headers;
		erase_protocol_response_headers(plan.response.headers);
		const bool h2 = server_is_h2(plan.request.version);
		for( const auto &field : *headers )
		{
			if( not h2 and (server_equal_ci(field.name, "Connection") or
				server_equal_ci(field.name, "Upgrade")) )
				continue;
			plan.response.headers.set(field.name, field.value);
		}
		plan.response.status = h2 ? http_exp::status::ok :
			http_exp::status::switching_protocols;
		if( not h2 ) plan.response.upgrade_protocol = "websocket";
		plan.accepted = true;
		plan.error.clear();
	}
	catch(...) {
		reject_upgrade(plan, exception_error(std::current_exception()),
			http_exp::status::internal_server_error);
	}
}

void complete_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept
{
	select_server_subprotocol(plan, options);
	validate_server_subprotocol(plan, options, deadline);
	if( not plan.accepted ) return;
	select_server_extensions(plan, options);
	validate_server_extensions(plan, options, deadline);
	finalize_server_upgrade_plan(plan, options, deadline);
}

server_upgrade_plan make_server_upgrade_plan(request_info request,
	const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline,
	bool extended_connect, bool permit_async_callbacks) noexcept
{
	server_upgrade_plan plan {.request = std::move(request)};
	try {
		const auto subprotocol_selectors =
			static_cast<size_t>(static_cast<bool>(options.subprotocol_selector)) +
			static_cast<size_t>(static_cast<bool>(
				options.async_subprotocol_selector));
		const auto extension_selectors =
			static_cast<size_t>(static_cast<bool>(options.extension_selector)) +
			static_cast<size_t>(static_cast<bool>(
				options.async_extension_selector));
		if( subprotocol_selectors > 1 or extension_selectors > 1 )
		{
			reject_upgrade(plan,
				make_system_error_code(std::errc::invalid_argument),
				http_exp::status::internal_server_error);
			return plan;
		}
		if( not permit_async_callbacks and
			(options.async_request_validator or
			 options.async_origin_validator or
			 options.async_subprotocol_selector or
			 options.async_extension_selector) )
		{
			reject_upgrade(plan,
				make_system_error_code(std::errc::operation_not_supported),
				http_exp::status::internal_server_error);
			return plan;
		}
		if( options.stream.read_buffer_size == 0 or
			options.stream.ping_interval < std::chrono::milliseconds::zero() or
			options.stream.compression.level < -1 or
			options.stream.compression.level > 9 )
		{
			reject_upgrade(plan,
				make_system_error_code(std::errc::invalid_argument),
				http_exp::status::internal_server_error);
			return plan;
		}
		auto opening = parse_opening_request(plan.request.method,
			plan.request.version, plan.request.request_headers,
			extended_connect);
		if( not opening )
		{
			reject_upgrade(plan, opening.error(),
				opening.error() == errc::unsupported_version ?
					http_exp::status::upgrade_required :
					http_exp::status::bad_request);
			return plan;
		}
		plan.opening = std::move(*opening);
		if( not supported_extension_offers(options.supported_extensions) )
		{
			reject_upgrade(plan,
				make_error_code(errc::unsupported_extension));
			return plan;
		}
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan,
				asio::error::make_error_code(asio::error::timed_out));
			return plan;
		}
		if( options.request_validator )
		{
			try {
				if( auto rejection = options.request_validator(plan.request) )
				{
					reject_upgrade(plan, std::move(*rejection),
						make_error_code(errc::handshake_rejected));
					return plan;
				}
			}
			catch(...) {
				reject_upgrade(plan, exception_error(std::current_exception()),
					http_exp::status::internal_server_error);
				plan.preserve_error_on_write_failure = true;
				return plan;
			}
		}
		if( options.origin_validator )
		{
			try {
				const auto origin =
					plan.request.request_headers.find_first("Origin");
				if( auto rejection = options.origin_validator(origin) )
				{
					reject_upgrade(plan, std::move(*rejection),
						make_error_code(errc::handshake_rejected));
					return plan;
				}
			}
			catch(...) {
				reject_upgrade(plan, exception_error(std::current_exception()),
					http_exp::status::internal_server_error);
				plan.preserve_error_on_write_failure = true;
				return plan;
			}
		}
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan,
				asio::error::make_error_code(asio::error::timed_out));
			return plan;
		}
		plan.accepted = true;
		plan.error.clear();
		if( not permit_async_callbacks )
			complete_server_upgrade_plan(plan, options, deadline);
	}
	catch(...) {
		reject_upgrade(plan, exception_error(std::current_exception()),
			http_exp::status::internal_server_error);
	}
	return plan;
}

http_exp::server_config websocket_http_config(http_exp::server_config config)
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	config.v2.enable_extended_connect = true;
#endif
	return config;
}

server_config validate_server_config(server_config config)
{
	if( config.default_upgrade.stream.read_buffer_size == 0 or
		config.default_upgrade.stream.ping_interval <
			std::chrono::milliseconds::zero() or
		config.default_upgrade.stream.compression.level < -1 or
		config.default_upgrade.stream.compression.level > 9 )
		throw std::invalid_argument(
			"riwo::websocket_exp::basic_server: invalid stream config");
	config.http = websocket_http_config(std::move(config.http));
	return config;
}

} //namespace riwo::websocket_exp::detail
