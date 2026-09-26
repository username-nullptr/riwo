// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/server.h>

namespace riwo::websocket::detail
{

bool server_ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept
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

bool server_deadline_expired(std::chrono::steady_clock::time_point deadline) noexcept
{
	return std::chrono::steady_clock::now() >= deadline;
}

std::chrono::milliseconds server_remaining_timeout
(std::chrono::steady_clock::time_point deadline) noexcept
{
	const auto now = std::chrono::steady_clock::now();
	if( now >= deadline )
		return std::chrono::milliseconds::zero();

	auto remaining = std::chrono::duration_cast
		<std::chrono::milliseconds>(deadline - now);

	return remaining > std::chrono::milliseconds::zero() ?
		remaining : std::chrono::milliseconds(1);
}

bool valid_rejection_status(http::status_enum status) noexcept
{
	const auto value = static_cast<uint32_t>(status);
	return value >= 200 and value <= 599 and
		status != http::status::switching_protocols;
}

void erase_protocol_response_headers(http::headers &headers) noexcept
{
	headers.erase(http::header::connection);
	headers.erase(http::header::upgrade);

	headers.erase(sec_websocket_accept);
	headers.erase(sec_websocket_version);

	headers.erase(sec_websocket_protocol);
	headers.erase(sec_websocket_extensions);

	headers.erase(http::header::content_length);
	headers.erase(http::header::transfer_encoding);
}

void reject_upgrade(server_upgrade_plan &plan, error_code error,
	http::status_enum status)
{
	plan.accepted = false;
	plan.error = error;
	plan.status = status;

	if( status == http::status::upgrade_required )
		plan.headers[sec_websocket_version] = "13";
}

void reject_upgrade(server_upgrade_plan &plan, upgrade_rejection rejection, error_code error)
{
	plan.accepted = false;
	plan.error = error;

	plan.status = valid_rejection_status(rejection.status) ?
		rejection.status : http::status::internal_server_error;

	plan.headers = std::move(rejection.headers);
	erase_protocol_response_headers(plan.headers);
	plan.body = std::move(rejection.body);
}

void reject_selector_exception
(server_upgrade_plan &plan, const std::exception_ptr &exception) noexcept
{
	reject_upgrade(plan, exception_error(exception),
		http::status::internal_server_error);
	plan.preserve_error_on_write_failure = true;
}

void select_server_subprotocol
(server_upgrade_plan &plan, const upgrade_options &options) noexcept
{
	try {
		if( options.subprotocol_selector )
		{
			plan.response.subprotocol = options.subprotocol_selector (
				plan.request, plan.opening.subprotocols
			);
		}
		else
		{
			for(const auto &offered : plan.opening.subprotocols)
			{
				if( std::ranges::find(options.supported_subprotocols, offered) !=
					options.supported_subprotocols.end() )
				{
					plan.response.subprotocol = offered;
					break;
				}
			}
		}
	}
	catch(...) {
		reject_selector_exception(plan, std::current_exception());
	}
}

void validate_server_subprotocol(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted )
		return ;

	if( server_deadline_expired(deadline) )
	{
		reject_upgrade(plan, asio::error::timed_out);
		return ;
	}
	if( plan.response.subprotocol and
		(std::ranges::find(plan.opening.subprotocols,
			*plan.response.subprotocol) == plan.opening.subprotocols.end() or
		 std::ranges::find(options.supported_subprotocols,
			*plan.response.subprotocol) == options.supported_subprotocols.end()) )
	{
		reject_upgrade(plan, make_error_code(errc::unsupported_subprotocol));
		return ;
	}
	if( options.require_subprotocol and not plan.response.subprotocol )
		reject_upgrade(plan, make_error_code(errc::unsupported_subprotocol));
}

void select_server_extensions(server_upgrade_plan &plan, const upgrade_options &options) noexcept
{
	try {
		if( options.extension_selector )
		{
			plan.response.extensions = options.extension_selector (
				plan.request, plan.opening.extensions
			);
		}
		else if( not options.supported_extensions.empty() )
		{
			for(const auto &offered : plan.opening.extensions)
			{
				for(const auto &policy : options.supported_extensions)
				{
					if( auto selected = negotiate_permessage_deflate(offered, policy) )
					{
						plan.response.extensions = {std::move(*selected)};
						break;
					}
				}
				if( not plan.response.extensions.empty() )
					break;
			}
		}
	}
	catch(...) {
		reject_selector_exception(plan, std::current_exception());
	}
}

void validate_server_extensions(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted )
		return ;

	if( not plan.response.extensions.empty() and
		(not supported_extension_response(plan.response.extensions,
			plan.opening.extensions) or options.supported_extensions.empty()) )
	{
		reject_upgrade(plan, make_error_code(errc::unsupported_extension));
		return ;
	}
	if( server_deadline_expired(deadline) )
		reject_upgrade(plan, asio::error::timed_out);
}

void finalize_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline) noexcept
{
	if( not plan.accepted )
		return ;
	try
	{
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan, asio::error::timed_out);
			return ;
		}
		auto protocol_headers = make_opening_response_headers (
			plan.opening, plan.response
		);
		if( not protocol_headers )
		{
			reject_upgrade(plan, protocol_headers.error());
			return ;
		}
		plan.headers = options.response_headers;
		erase_protocol_response_headers(plan.headers);

		for(auto &[name, value] : *protocol_headers)
			plan.headers[name] = value;

		plan.status = http::status::switching_protocols;
		plan.accepted = true;
		plan.error.clear();
	}
	catch(...)
	{
		reject_upgrade(plan, exception_error(std::current_exception()),
			http::status::internal_server_error
		);
	}
}

void complete_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline) noexcept
{
	select_server_subprotocol(plan, options);
	validate_server_subprotocol(plan, options, deadline);

	if( not plan.accepted )
		return ;

	select_server_extensions(plan, options);
	validate_server_extensions(plan, options, deadline);
	finalize_server_upgrade_plan(plan, options, deadline);
}

server_upgrade_plan make_server_upgrade_plan(request_info request, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline, bool permit_async_callbacks) noexcept
{
	server_upgrade_plan plan {.request = std::move(request)};
	try {
		const auto subprotocol_selectors =
			static_cast<size_t>(static_cast<bool>(options.subprotocol_selector)) +
			static_cast<size_t>(static_cast<bool>(options.async_subprotocol_selector));

		const auto extension_selectors =
			static_cast<size_t>(static_cast<bool>(options.extension_selector)) +
			static_cast<size_t>(static_cast<bool>(options.async_extension_selector));

		if( subprotocol_selectors > 1 or extension_selectors > 1 )
		{
			reject_upgrade(plan, make_system_error_code(std::errc::invalid_argument),
				http::status::internal_server_error);
			return plan;
		}
		if( not permit_async_callbacks and
			(options.async_request_validator or options.async_origin_validator or
			 options.async_subprotocol_selector or options.async_extension_selector) )
		{
			reject_upgrade(plan,
				make_system_error_code(std::errc::operation_not_supported),
				http::status::internal_server_error
			);
			return plan;
		}
		if( options.stream.read_buffer_size == 0 or
			options.stream.ping_interval < std::chrono::milliseconds::zero() or
			options.stream.compression.level < -1 or
			options.stream.compression.level > 9 )
		{
			reject_upgrade(plan, make_system_error_code(std::errc::invalid_argument),
				http::status::internal_server_error
			);
			return plan;
		}
		auto opening = parse_opening_request(plan.request.method,
			plan.request.version, plan.request.request_headers
		);
		if( not opening )
		{
			reject_upgrade(plan, opening.error(),
				opening.error() == errc::unsupported_version ?
					http::status::upgrade_required : http::status::bad_request
			);
			return plan;
		}
		plan.opening = std::move(*opening);

		if( not supported_extension_offers(options.supported_extensions) )
		{
			reject_upgrade(plan, make_error_code(errc::unsupported_extension));
			return plan;
		}
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan, asio::error::timed_out);
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
			catch(...)
			{
				reject_upgrade(plan, exception_error(std::current_exception()),
					http::status::internal_server_error
				);
				plan.preserve_error_on_write_failure = true;
				return plan;
			}
		}
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan, asio::error::timed_out);
			return plan;
		}
		if( options.origin_validator )
		{
			try {
				optional<std::string_view> origin;
				if( auto it = plan.request.request_headers.find(http::header::origin);
					it != plan.request.request_headers.end() )
					origin = it->second.to_string();

				if( auto rejection = options.origin_validator(origin) )
				{
					reject_upgrade(plan, std::move(*rejection),
						make_error_code(errc::handshake_rejected));
					return plan;
				}
			}
			catch(...)
			{
				reject_upgrade(plan, exception_error(std::current_exception()),
					http::status::internal_server_error
				);
				plan.preserve_error_on_write_failure = true;
				return plan;
			}
		}
		if( server_deadline_expired(deadline) )
		{
			reject_upgrade(plan, asio::error::timed_out);
			return plan;
		}
		plan.accepted = true;
		plan.error.clear();

		if( not permit_async_callbacks )
			complete_server_upgrade_plan(plan, options, deadline);
		return plan;
	}
	catch(...)
	{
		reject_upgrade(plan, exception_error(std::current_exception()),
			http::status::internal_server_error
		);
	}
	return plan;
}

} //namespace riwo::websocket::detail
