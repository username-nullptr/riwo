// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H
#define RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H

#include <riwo/websocket_exp/protocol/handshake.h>
#include <riwo/websocket_exp/detail/permessage_deflate.h>
#include <riwo/websocket_exp/detail/secure_random.h>

namespace riwo::websocket_exp::detail
{

inline bool equal_ci(std::string_view lhs, std::string_view rhs) noexcept
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

inline sys_expected<url> websocket_transport_url(const url &endpoint) noexcept
{
	try {
		if( not endpoint.is_valid() or endpoint.host().empty() or
			endpoint.has_fragment() )
			return sys_unexpected(make_system_error_code(
				std::errc::invalid_argument));
		auto scheme = strtls::to_lower(endpoint.protocol());
		if( scheme != "ws" and scheme != "wss" and
			scheme != "http" and scheme != "https" )
			return sys_unexpected(make_system_error_code(
				std::errc::protocol_not_supported));
		auto text = endpoint.to_string();
		auto separator = text.find("://");
		if( separator == std::string::npos )
			return sys_unexpected(make_system_error_code(
				std::errc::invalid_argument));
		text.replace(0, separator, scheme == "wss" or scheme == "https" ?
			"https" : "http");
		return url(std::move(text));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

inline bool extended_connect(const http_exp::client_config &config) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	return config.protocols.requirement ==
		http_exp::protocol_requirement::require_http2;
#else
	ignore_unused(config);
	return false;
#endif
}

inline http_exp::client_config websocket_http_config(
	http_exp::client_config config)
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( config.protocols.requirement ==
		http_exp::protocol_requirement::require_http2 )
		config.h2.enable_extended_connect = true;
	else
#endif
		config.protocols.requirement =
			http_exp::protocol_requirement::require_http1;
	config.redirects.max_redirects = 0;
	config.automatic_decompression = false;
	return config;
}

struct prepared_open
{
	prepared_open(http_exp::request request_value, http_exp::body_spec spec_value,
		opening_request opening_value, stream_config stream_value) :
		request(std::move(request_value)), spec(std::move(spec_value)),
		opening(std::move(opening_value)), stream(std::move(stream_value)) {}

	http_exp::request request;
	http_exp::body_spec spec;
	opening_request opening;
	stream_config stream;
};

inline sys_expected<prepared_open> prepare_open(connect_request request,
	const client_config &config, bool use_extended_connect) noexcept
{
	try {
		if( request.handshake_timeout.value_or(config.handshake_timeout) <=
			std::chrono::milliseconds::zero() )
			return sys_unexpected(asio::error::make_error_code(
				asio::error::timed_out));
		auto target = websocket_transport_url(request.endpoint);
		if( not target ) return sys_unexpected(target.error());
		if( not supported_extension_offers(request.extensions) )
			return sys_unexpected(make_error_code(errc::unsupported_extension));

		std::array<std::byte,16> nonce {};
		if( auto random = secure_random_bytes(asio::buffer(nonce)); not random )
			return sys_unexpected(random.error());
		auto key = make_client_key(nonce);
		if( not key ) return sys_unexpected(key.error());

		opening_request opening {
			.key = std::move(*key),
			.subprotocols = std::move(request.subprotocols),
			.extensions = std::move(request.extensions),
		};
		auto version = http_exp::protocol::v1_1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( use_extended_connect ) version = http_exp::protocol::v2;
#endif
		auto handshake = make_opening_request_headers(opening, version);
		if( not handshake ) return sys_unexpected(handshake.error());

		http_exp::request result(
			use_extended_connect ? http_exp::method::connect :
				http_exp::method::get, std::move(*target));
		for( const auto &field : request.headers )
			result.headers().add(field.name, field.value);
		for( const auto &field : *handshake )
		{
			if( not use_extended_connect and
				(equal_ci(field.name, "Connection") or
				 equal_ci(field.name, "Upgrade")) )
				continue;
			result.headers().set(field.name, field.value);
		}
		if( use_extended_connect ) result.connect_protocol("websocket");
		else result.upgrade_protocol("websocket");

		auto spec = use_extended_connect ? http_exp::body_spec::streaming() :
			http_exp::body_spec::empty();
		return prepared_open(std::move(result), std::move(spec),
			std::move(opening), request.stream_options.value_or(config.stream));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace riwo::websocket_exp::detail

namespace riwo::websocket_exp
{

template <core_concepts::exec Exec>
class basic_client<Exec>::impl
{
public:
	impl(executor_type executor_value, config_t config_value) :
		m_executor(std::move(executor_value)), m_config(std::move(config_value)),
		m_use_extended_connect(detail::extended_connect(m_config.http)),
		m_http(m_executor, detail::websocket_http_config(m_config.http)) {}

	executor_type m_executor;
	config_t m_config;
	bool m_use_extended_connect = false;
	http_client_t m_http;
};

namespace detail
{

template <core_concepts::exec Exec>
void open_sync(const std::shared_ptr<typename basic_client<Exec>::impl> &self,
	connect_request request, basic_open_diagnostics<Exec> *diagnostics,
	basic_stream<Exec> &result, error_code &error) noexcept
{
	try {
		if( diagnostics ) diagnostics->endpoint = request.endpoint;
		auto prepared = prepare_open(std::move(request), self->m_config,
			self->m_use_extended_connect);
		if( not prepared ) { error = prepared.error(); return; }
		result = basic_stream<Exec>(self->m_executor, prepared->stream);
		auto exchange = self->m_http.open(std::move(prepared->request),
			std::move(prepared->spec), error);
		if( error ) return;
		auto response = exchange.wait_response(error);
		if( error ) return;
		if( diagnostics )
		{
			diagnostics->response = response;
			diagnostics->selected_protocol = exchange.selected_protocol();
		}
		auto opening = parse_opening_response(response.status, response.headers,
			prepared->opening, exchange.selected_protocol());
		if( not opening ) { error = opening.error(); return; }
		auto channel = exchange.release_duplex(error);
		if( error ) return;
		result.adopt(std::make_shared<http_exp::basic_duplex_channel<Exec>>(
			std::move(channel)), adopt_options {
				.stream_role = role::client,
				.negotiated_subprotocol = opening->subprotocol.value_or(""),
				.negotiated_extensions = std::move(opening->extensions),
			}, error);
	}
	catch(...) { error = exception_error(std::current_exception()); }
}

template <core_concepts::exec Exec>
asio::awaitable<optional<std::tuple<error_code,basic_stream<Exec>>>,Exec> co_open(
	std::shared_ptr<typename basic_client<Exec>::impl> self,
	connect_request request, basic_open_diagnostics<Exec> *diagnostics)
{
	basic_stream<Exec> result(self->m_executor,
		request.stream_options.value_or(self->m_config.stream));
	try {
		if( diagnostics ) diagnostics->endpoint = request.endpoint;
		auto prepared = prepare_open(std::move(request), self->m_config,
			self->m_use_extended_connect);
		if( not prepared ) co_return std::tuple {
			prepared.error(), std::move(result)};
		result = basic_stream<Exec>(self->m_executor, prepared->stream);
		auto [open_error, exchange] = co_await self->m_http.open(
			std::move(prepared->request), std::move(prepared->spec),
			asio::as_tuple(asio::use_awaitable_t<Exec>{}));
		if( open_error ) co_return std::tuple {open_error, std::move(result)};
		auto [head_error, response] = co_await exchange.wait_response(
			asio::as_tuple(asio::use_awaitable_t<Exec>{}));
		if( head_error ) co_return std::tuple {head_error, std::move(result)};
		if( diagnostics )
		{
			diagnostics->response = response;
			diagnostics->selected_protocol = exchange.selected_protocol();
		}
		auto opening = parse_opening_response(response.status, response.headers,
			prepared->opening, exchange.selected_protocol());
		if( not opening ) co_return std::tuple {
			opening.error(), std::move(result)};
		auto [channel_error, channel] = co_await exchange.release_duplex(
			asio::as_tuple(asio::use_awaitable_t<Exec>{}));
		if( channel_error ) co_return std::tuple {
			channel_error, std::move(result)};
		error_code adopt_error;
		result.adopt(std::make_shared<http_exp::basic_duplex_channel<Exec>>(
			std::move(channel)), adopt_options {
				.stream_role = role::client,
				.negotiated_subprotocol = opening->subprotocol.value_or(""),
				.negotiated_extensions = std::move(opening->extensions),
			}, adopt_error);
		co_return std::tuple {adopt_error, std::move(result)};
	}
	catch(...) {
		co_return std::tuple {exception_error(std::current_exception()),
			std::move(result)};
	}
}

} //namespace detail

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client() requires
	core_concepts::match_sched<io_executor_t,executor_type> :
	basic_client(executor_type(riwo::get_executor())) {}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(config_t config) requires
	core_concepts::match_sched<io_executor_t,executor_type> :
	basic_client(executor_type(riwo::get_executor()), std::move(config)) {}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor, config_t config) requires (
	not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
	core_concepts::match_sched<Exec0,executor_type>) :
	m_impl(std::make_shared<impl>(get_executor_helper(
		std::forward<Exec0>(executor)), std::move(config))) {}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open_impl(connect_request_t request,
	diagnostics_t *diagnostics, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
	{
		stream_t result(m_impl->m_executor,
			request.stream_options.value_or(m_impl->m_config.stream));
		detail::open_sync(m_impl, std::move(request), diagnostics, result, token);
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		stream_t result(m_impl->m_executor,
			request.stream_options.value_or(m_impl->m_config.stream));
		detail::open_sync(m_impl, std::move(request), diagnostics, result, error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::basic_client::open");
		return result;
	}
	else
	{
		auto self = m_impl;
		return initiate_io<stream_t>(m_impl->m_executor,
		[self = std::move(self), request = std::move(request), diagnostics]
		(auto handler) mutable {
			auto executor = self->m_executor;
			asio::co_spawn(executor, detail::co_open(
				std::move(self), std::move(request), diagnostics),
			[handler = std::move(handler), executor]
			(std::exception_ptr exception,
			 optional<std::tuple<error_code,stream_t>> value) mutable {
				if( exception )
					std::move(handler)(exception_error(exception), stream_t(executor));
				else if( value )
					std::move(handler)(std::get<0>(*value),
						std::move(std::get<1>(*value)));
				else
					std::move(handler)(asio::error::operation_aborted,
						stream_t(executor));
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request, Token &&token)
	requires open_token_v<Token> {
	return open_impl(std::move(request), nullptr, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request,
	diagnostics_t &diagnostics, Token &&token) requires open_token_v<Token> {
	return open_impl(std::move(request), &diagnostics,
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, Token &&token)
	requires open_token_v<Token> {
	connect_request request;
	request.endpoint = std::move(endpoint);
	return open(std::move(request), std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, diagnostics_t &diagnostics,
	Token &&token) requires open_token_v<Token> {
	connect_request request;
	request.endpoint = std::move(endpoint);
	return open(std::move(request), diagnostics, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::config() const noexcept -> const config_t& {
	return m_impl->m_config;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() const noexcept -> const http_client_t& {
	return m_impl->m_http;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() noexcept -> http_client_t& {
	return m_impl->m_http;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->m_executor;
}

template <core_concepts::exec Exec>
void basic_client<Exec>::cancel() noexcept {}

} //namespace riwo::websocket_exp

#endif //RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H
