// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H
#define RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H

#include <riwo/websocket_exp/protocol/handshake.h>
#include <riwo/websocket_exp/detail/permessage_deflate.h>

namespace riwo::websocket_exp::detail
{

inline bool server_equal_ci(std::string_view lhs, std::string_view rhs) noexcept
{
	if( lhs.size() != rhs.size() ) return false;
	for( size_t index = 0; index < lhs.size(); ++index )
	{
		auto left = static_cast<unsigned char>(lhs[index]);
		auto right = static_cast<unsigned char>(rhs[index]);
		if( left >= 'A' and left <= 'Z' ) left += 'a' - 'A';
		if( right >= 'A' and right <= 'Z' ) right += 'a' - 'A';
		if( left != right ) return false;
	}
	return true;
}

inline bool header_has_token(const http_exp::fields &headers,
	std::string_view name, std::string_view wanted) noexcept
{
	for( const auto &field : headers.find_all(name) )
	{
		std::string_view value = field.value;
		while( not value.empty() )
		{
			auto comma = value.find(',');
			auto token = value.substr(0, comma);
			while( not token.empty() and (token.front() == ' ' or
				token.front() == '\t') ) token.remove_prefix(1);
			while( not token.empty() and (token.back() == ' ' or
				token.back() == '\t') ) token.remove_suffix(1);
			if( server_equal_ci(token, wanted) ) return true;
			if( comma == std::string_view::npos ) break;
			value.remove_prefix(comma + 1);
		}
	}
	return false;
}

inline bool server_is_h2(http_exp::protocol version) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	return version == http_exp::protocol::v2;
#else
	ignore_unused(version);
	return false;
#endif
}

struct server_open_plan
{
	request_info request {};
	opening_response opening {};
	http_exp::response_head response {};
};

template <core_concepts::exec Exec>
sys_expected<server_open_plan> prepare_server_open(
	const http_exp::basic_server_exchange<Exec> &exchange,
	const upgrade_options &options) noexcept
{
	try {
		if( options.handshake_timeout <= std::chrono::milliseconds::zero() )
			return sys_unexpected(asio::error::make_error_code(
				asio::error::timed_out));
		const auto &head = exchange.request();
		const bool h2 = server_is_h2(exchange.selected_protocol());
		auto opening = parse_opening_request(head.method,
			exchange.selected_protocol(), head.headers,
			h2 and head.connect_protocol and
				server_equal_ci(*head.connect_protocol, "websocket"));
		if( not opening ) return sys_unexpected(opening.error());

		server_open_plan plan;
		plan.request.method = head.method;
		plan.request.version = exchange.selected_protocol();
		plan.request.target = head.target;
		plan.request.request_headers = head.headers;
		plan.request.remote_address = exchange.remote_address();
		plan.request.remote_port = exchange.remote_port();
		if( options.request_validator )
		{
			auto rejection = options.request_validator(plan.request);
			if( rejection ) return sys_unexpected(
				make_error_code(errc::handshake_rejected));
		}

		if( options.subprotocol_selector )
			plan.opening.subprotocol = options.subprotocol_selector(
				plan.request, opening->subprotocols);
		else
		{
			for( const auto &supported : options.supported_subprotocols )
				if( std::ranges::find(opening->subprotocols, supported) !=
					opening->subprotocols.end() )
				{
					plan.opening.subprotocol = supported;
					break;
				}
		}
		if( options.require_subprotocol and not plan.opening.subprotocol )
			return sys_unexpected(make_error_code(
				errc::unsupported_subprotocol));

		if( options.extension_selector )
			plan.opening.extensions = options.extension_selector(
				plan.request, opening->extensions);
		else
		{
			for( const auto &offered : opening->extensions )
			{
				for( const auto &supported : options.supported_extensions )
					if( auto selected = negotiate_permessage_deflate(
						offered, supported) )
					{
						plan.opening.extensions.push_back(
							std::move(*selected));
						break;
					}
				if( not plan.opening.extensions.empty() ) break;
			}
		}
		if( not supported_extension_response(
			plan.opening.extensions, opening->extensions) )
			return sys_unexpected(make_error_code(errc::unsupported_extension));

		auto headers = make_opening_response_headers(*opening, plan.opening,
			exchange.selected_protocol());
		if( not headers ) return sys_unexpected(headers.error());
		plan.response.status = h2 ? http_exp::status::ok :
			http_exp::status::switching_protocols;
		plan.response.headers = options.response_headers;
		for( const auto &field : *headers )
		{
			if( not h2 and (server_equal_ci(field.name, "Connection") or
				server_equal_ci(field.name, "Upgrade")) )
				continue;
			plan.response.headers.set(field.name, field.value);
		}
		if( not h2 ) plan.response.upgrade_protocol = "websocket";
		return plan;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

template <core_concepts::exec Exec>
void reject_upgrade(http_exp::basic_server_exchange<Exec> &exchange,
	error_code &write_error) noexcept
{
	http_exp::response_head response;
	response.status = http_exp::status::bad_request;
	static constexpr std::string_view body = "Invalid WebSocket handshake";
	exchange.respond(std::move(response), asio::buffer(body), write_error);
}

template <core_concepts::exec Exec>
void upgrade_sync(http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, basic_accept_result<Exec> &result,
	error_code &error) noexcept
{
	try {
		result.stream = basic_stream<Exec>(exchange.get_executor(), options.stream);
		auto plan = prepare_server_open(exchange, options);
		if( not plan )
		{
			auto handshake_error = plan.error();
			reject_upgrade(exchange, error);
			if( not error ) error = handshake_error;
			return;
		}
		result.request = plan->request;
		auto channel = exchange.accept_duplex(std::move(plan->response), error);
		if( error ) return;
		result.handshake.subprotocol = plan->opening.subprotocol.value_or("");
		result.handshake.extensions = plan->opening.extensions;
		result.stream.adopt(
			std::make_shared<http_exp::basic_duplex_channel<Exec>>(
				std::move(channel)), adopt_options {
					.stream_role = role::server,
					.negotiated_subprotocol = result.handshake.subprotocol,
					.negotiated_extensions = result.handshake.extensions,
				}, error);
	}
	catch(...) { error = exception_error(std::current_exception()); }
}

template <core_concepts::exec Exec>
asio::awaitable<optional<std::tuple<error_code,basic_accept_result<Exec>>>,Exec>
co_upgrade(http_exp::basic_server_exchange<Exec> *exchange,
	upgrade_options options)
{
	basic_accept_result<Exec> result(exchange->get_executor());
	try {
		result.stream = basic_stream<Exec>(exchange->get_executor(), options.stream);
		auto plan = prepare_server_open(*exchange, options);
		if( not plan )
		{
			auto handshake_error = plan.error();
			http_exp::response_head response;
			response.status = http_exp::status::bad_request;
			static constexpr std::string_view body =
				"Invalid WebSocket handshake";
			auto [write_error] = co_await exchange->respond(
				std::move(response), asio::buffer(body),
				asio::as_tuple(asio::use_awaitable_t<Exec>{}));
			co_return std::tuple {write_error ? write_error : handshake_error,
				std::move(result)};
		}
		result.request = plan->request;
		auto [accept_error, channel] = co_await exchange->accept_duplex(
			std::move(plan->response),
			asio::as_tuple(asio::use_awaitable_t<Exec>{}));
		if( accept_error ) co_return std::tuple {
			accept_error, std::move(result)};
		result.handshake.subprotocol = plan->opening.subprotocol.value_or("");
		result.handshake.extensions = plan->opening.extensions;
		error_code adopt_error;
		result.stream.adopt(
			std::make_shared<http_exp::basic_duplex_channel<Exec>>(
				std::move(channel)), adopt_options {
					.stream_role = role::server,
					.negotiated_subprotocol = result.handshake.subprotocol,
					.negotiated_extensions = result.handshake.extensions,
				}, adopt_error);
		co_return std::tuple {adopt_error, std::move(result)};
	}
	catch(...) {
		co_return std::tuple {exception_error(std::current_exception()),
			std::move(result)};
	}
}

} //namespace riwo::websocket_exp::detail

namespace riwo::websocket_exp
{

template <core_concepts::exec Exec>
bool is_upgrade_request(
	const http_exp::basic_server_exchange<Exec> &exchange) noexcept
{
	const auto &request = exchange.request();
	if( detail::server_is_h2(exchange.selected_protocol()) )
		return request.method.known() == http_exp::method::connect and
			request.connect_protocol and
			detail::server_equal_ci(*request.connect_protocol, "websocket");
	return request.method.known() == http_exp::method::get and
		detail::header_has_token(request.headers, "Connection", "Upgrade") and
		detail::header_has_token(request.headers, "Upgrade", "websocket");
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http_exp::basic_server_exchange<Exec> &exchange, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>> {
	return upgrade(exchange, upgrade_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>>
{
	using result_t = basic_accept_result<Exec>;
	if constexpr( is_error_code_token_v<Token> )
	{
		result_t result(exchange.get_executor());
		detail::upgrade_sync(exchange, std::move(options), result, token);
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		result_t result(exchange.get_executor());
		detail::upgrade_sync(exchange, std::move(options), result, error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::upgrade");
		return result;
	}
	else
	{
		auto executor = exchange.get_executor();
		return initiate_io<result_t>(executor,
		[&exchange, options = std::move(options), executor]
		(auto handler) mutable {
			asio::co_spawn(executor, detail::co_upgrade(
				&exchange, std::move(options)),
			[handler = std::move(handler), executor]
			(std::exception_ptr exception,
			 optional<std::tuple<error_code,result_t>> value) mutable {
				if( exception )
					std::move(handler)(exception_error(exception),
						result_t(executor));
				else if( value )
					std::move(handler)(std::get<0>(*value),
						std::move(std::get<1>(*value)));
				else
					std::move(handler)(asio::error::operation_aborted,
						result_t(executor));
			});
		}, std::forward<Token>(token));
	}
}

} //namespace riwo::websocket_exp

#endif //RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H
