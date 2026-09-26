// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_CONNECTOR_H
#define RIWO_HTTP_CLIENT_DETAIL_CONNECTOR_H

#include <riwo/http/client/detail/proxy_tunnel.h>
#include <riwo/http/utils/tcp_connection.h>
#include <riwo/http/utils/tls_connection.h>

namespace riwo::http
{

#if RIWO_OPENSSL_SUPPORT
namespace detail {
[[nodiscard]] RIWO_HTTP_API asio::ssl::context &default_ssl_context() noexcept;
} //namespace detail
#endif //RIWO_OPENSSL_SUPPORT

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_connector<Exec>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(executor_t exec) :
		m_exec(std::move(exec)) {}

#if RIWO_OPENSSL_SUPPORT
	impl(executor_t exec, asio::ssl::context &tls_context) :
		m_tls_context(&tls_context), m_exec(std::move(exec)) {}

	asio::ssl::context *m_tls_context =
		&detail::default_ssl_context();
#endif //RIWO_OPENSSL_SUPPORT

	executor_t m_exec {};
};

template <core_concepts::exec Exec>
basic_connector<Exec>::basic_connector
(core_concepts::match_sched<executor_t> auto &&exec) :
	m_impl(new impl(executor_t(get_executor_helper(std::forward<decltype(exec)>(exec)))))
{

}

#if RIWO_OPENSSL_SUPPORT
template <core_concepts::exec Exec>
basic_connector<Exec>::basic_connector
(core_concepts::match_sched<executor_t> auto &&exec, asio::ssl::context &tls_context) :
	m_impl(new impl(executor_t(get_executor_helper(std::forward<decltype(exec)>(exec))), tls_context))
{

}
#endif //RIWO_OPENSSL_SUPPORT

template <core_concepts::exec Exec>
basic_connector<Exec>::~basic_connector()
{
	delete m_impl;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_connector<Exec>::connect(const connect_target &target, Token &&token) noexcept
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,connection_ptr>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = do_connect(target);
		token = result ? error_code{} : result.error();
		return result ? std::move(*result) : connection_ptr{};
	}
	else if constexpr( is_sync_opt_token_v<Token> )
		return do_connect(target);
	else
	{
		return initiate_expected<connection_ptr>(get_executor(),
		[this, target]() mutable -> awaitable<sys_expected<connection_ptr>> {
			co_return co_await co_do_connect(target);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
basic_connector<Exec>::executor_t basic_connector<Exec>::get_executor() const noexcept
{
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
sys_expected<typename basic_connector<Exec>::connection_ptr>
basic_connector<Exec>::do_connect(const connect_target &target) noexcept
{
	try {
		if( target.host.empty() or target.port == 0 )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( target.tunnel )
		{
			if( auto error = detail::validate_proxy_tunnel(target, *target.tunnel) )
				return sys_unexpected(error);

			if( target.security == security_mode::tls and target.tunnel->security == security_mode::tls )
				return sys_unexpected(make_system_error_code(std::errc::operation_not_supported));
		}
		using resolver_t = asio::ip::basic_resolver<asio::ip::tcp,executor_t>;
		resolver_t resolver(m_impl->m_exec);

		error_code error {};
		const auto &connect_host = target.tunnel ? target.tunnel->host : target.host;
		const auto connect_port = target.tunnel ? target.tunnel->port : target.port;

		auto results = resolver.resolve(connect_host,
			std::to_string(connect_port), error
		);
		if( error )
			return sys_unexpected(error);

		if( results.empty() )
			return sys_unexpected(make_error_code(errc::host_not_found));

		if( target.security == security_mode::plain and
			(not target.tunnel or target.tunnel->security == security_mode::plain) )
		{
			typename basic_tcp_connection<executor_t>::socket_t socket(m_impl->m_exec);
			asio::connect(socket, results, error);

			if( error )
				return sys_unexpected(error);

			socket.set_option(asio::ip::tcp::no_delay(target.no_delay), error);
			if( error )
				return sys_unexpected(error);

			if( target.tunnel )
			{
				error = detail::sync_proxy_connect(socket, target, *target.tunnel);
				if( error )
					return sys_unexpected(error);
			}
			return connection_ptr(
				std::make_shared<basic_tcp_connection<executor_t>>(std::move(socket))
			);
		}
#if RIWO_OPENSSL_SUPPORT
		if( target.security == security_mode::plain and target.tunnel and
			target.tunnel->security == security_mode::tls )
		{
			typename basic_tls_connection<executor_t>::socket_t socket (
				m_impl->m_exec, *m_impl->m_tls_context
			);
			error_code address_error {};
			ignore_unused(asio::ip::make_address(target.tunnel->host, address_error));

			if( address_error and
				not ::SSL_set_tlsext_host_name(socket.native_handle(), target.tunnel->host.c_str()) )
				return sys_unexpected(make_system_error_code(std::errc::protocol_error));

			socket.set_verify_callback (
				asio::ssl::host_name_verification(target.tunnel->host), error
			);
			if( error )
				return sys_unexpected(error);

			asio::connect(socket.next_layer(), results, error);
			if( error )
				return sys_unexpected(error);

			socket.next_layer().set_option (
				asio::ip::tcp::no_delay(target.no_delay), error
			);
			if( error )
				return sys_unexpected(error);

			socket.handshake(asio::ssl::stream_base::client, error);
			if( error )
				return sys_unexpected(error);

			error = detail::sync_proxy_connect(socket, target, *target.tunnel);
			if( error )
				return sys_unexpected(error);

			return connection_ptr (
				std::make_shared<basic_tls_connection<executor_t>>(std::move(socket))
			);
		}
		if( target.security == security_mode::tls )
		{
			typename basic_tls_connection<executor_t>::socket_t socket (
				m_impl->m_exec, *m_impl->m_tls_context
			);
			error_code address_error {};
			ignore_unused(asio::ip::make_address(target.host, address_error));

			if( address_error and
				not ::SSL_set_tlsext_host_name(socket.native_handle(), target.host.c_str()) )
				return sys_unexpected(make_system_error_code(std::errc::protocol_error));

			socket.set_verify_callback (
				asio::ssl::host_name_verification(target.host), error
			);
			if( error )
				return sys_unexpected(error);

			asio::connect(socket.next_layer(), results, error);
			if( error )
				return sys_unexpected(error);

			socket.next_layer().set_option (
				asio::ip::tcp::no_delay(target.no_delay), error
			);
			if( error )
				return sys_unexpected(error);

			if( target.tunnel )
			{
				error = detail::sync_proxy_connect(
					socket.next_layer(), target, *target.tunnel);
				if( error )
					return sys_unexpected(error);
			}
			socket.handshake(asio::ssl::stream_base::client, error);
			if( error )
				return sys_unexpected(error);

			return connection_ptr (
				std::make_shared<basic_tls_connection<executor_t>>(std::move(socket))
			);
		}
#endif //RIWO_OPENSSL_SUPPORT
		return sys_unexpected(make_system_error_code(std::errc::operation_not_supported));
	}
	catch(const std::system_error &ex) {
		return sys_unexpected(ex.code());
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

template <core_concepts::exec Exec>
awaitable<sys_expected<typename basic_connector<Exec>::connection_ptr>>
basic_connector<Exec>::co_do_connect(const connect_target &target) noexcept
{
	try {
		if( target.host.empty() or target.port == 0 )
			co_return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( target.tunnel )
		{
			if( auto error = detail::validate_proxy_tunnel(target, *target.tunnel) )
				co_return sys_unexpected(error);

			if( target.security == security_mode::tls and target.tunnel->security == security_mode::tls )
				co_return sys_unexpected(make_system_error_code(std::errc::operation_not_supported));
		}
		using resolver_t = asio::ip::basic_resolver<asio::ip::tcp,executor_t>;
		resolver_t resolver(m_impl->m_exec);

		error_code error {};
		const auto &connect_host = target.tunnel ? target.tunnel->host : target.host;
		const auto connect_port = target.tunnel ? target.tunnel->port : target.port;

		auto results = co_await resolver.async_resolve(connect_host,
			std::to_string(connect_port), asio::redirect_error(use_awaitable, error)
		);
		if( error )
			co_return sys_unexpected(error);

		if( results.empty() )
			co_return sys_unexpected(make_error_code(errc::host_not_found));

		if( target.security == security_mode::plain and
			(not target.tunnel or target.tunnel->security == security_mode::plain) )
		{
			typename basic_tcp_connection<executor_t>::socket_t socket(m_impl->m_exec);
			co_await asio::async_connect(socket, results,
				asio::redirect_error(use_awaitable, error)
			);
			if( error )
				co_return sys_unexpected(error);

			socket.set_option(asio::ip::tcp::no_delay(target.no_delay), error);
			if( error )
				co_return sys_unexpected(error);

			if( target.tunnel )
			{
				error = co_await detail::async_proxy_connect (
					socket, target, *target.tunnel
				);
				if( error )
					co_return sys_unexpected(error);
			}
			co_return connection_ptr (
				std::make_shared<basic_tcp_connection<executor_t>>(std::move(socket))
			);
		}
#if RIWO_OPENSSL_SUPPORT
		if( target.security == security_mode::plain and target.tunnel and
			target.tunnel->security == security_mode::tls )
		{
			typename basic_tls_connection<executor_t>::socket_t socket (
				m_impl->m_exec, *m_impl->m_tls_context
			);
			error_code address_error {};
			ignore_unused(asio::ip::make_address(target.tunnel->host, address_error));

			if( address_error and
				not ::SSL_set_tlsext_host_name(socket.native_handle(), target.tunnel->host.c_str()) )
				co_return sys_unexpected(make_system_error_code(std::errc::protocol_error));

			socket.set_verify_callback (
				asio::ssl::host_name_verification(target.tunnel->host), error
			);
			if( error )
				co_return sys_unexpected(error);

			co_await asio::async_connect(socket.next_layer(), results,
				asio::redirect_error(use_awaitable, error)
			);
			if( error )
				co_return sys_unexpected(error);

			socket.next_layer().set_option (
				asio::ip::tcp::no_delay(target.no_delay), error
			);
			if( error )
				co_return sys_unexpected(error);

			co_await socket.async_handshake(asio::ssl::stream_base::client,
				asio::redirect_error(use_awaitable, error)
			);
			if( error )
				co_return sys_unexpected(error);

			error = co_await detail::async_proxy_connect (
				socket, target, *target.tunnel
			);
			if( error )
				co_return sys_unexpected(error);

			co_return connection_ptr (
				std::make_shared<basic_tls_connection<executor_t>>(std::move(socket))
			);
		}
		if( target.security == security_mode::tls )
		{
			typename basic_tls_connection<executor_t>::socket_t socket (
				m_impl->m_exec, *m_impl->m_tls_context
			);
			error_code address_error {};
			ignore_unused(asio::ip::make_address(target.host, address_error));

			if( address_error and
				not ::SSL_set_tlsext_host_name(socket.native_handle(), target.host.c_str()) )
				co_return sys_unexpected(make_system_error_code(std::errc::protocol_error));

			socket.set_verify_callback (
				asio::ssl::host_name_verification(target.host), error
			);
			if( error )
				co_return sys_unexpected(error);

			co_await asio::async_connect(socket.next_layer(), results,
				asio::redirect_error(use_awaitable, error)
			);
			if( error )
				co_return sys_unexpected(error);

			socket.next_layer().set_option (
				asio::ip::tcp::no_delay(target.no_delay), error
			);
			if( error )
				co_return sys_unexpected(error);

			if( target.tunnel )
			{
				error = co_await detail::async_proxy_connect (
					socket.next_layer(), target, *target.tunnel
				);
				if( error )
					co_return sys_unexpected(error);
			}
			co_await socket.async_handshake(asio::ssl::stream_base::client,
				asio::redirect_error(use_awaitable, error)
			);
			if( error )
				co_return sys_unexpected(error);

			co_return connection_ptr (
				std::make_shared<basic_tls_connection<executor_t>>(std::move(socket))
			);
		}
#endif //RIWO_OPENSSL_SUPPORT
		co_return sys_unexpected(make_system_error_code(std::errc::operation_not_supported));
	}
	catch(const std::system_error &ex) {
		co_return sys_unexpected(ex.code());
	}
	catch(const std::bad_alloc&) {
		co_return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	co_return sys_unexpected(make_system_error_code(std::errc::io_error));
}

} //namespace riwo::http


#endif //RIWO_HTTP_CLIENT_DETAIL_CONNECTOR_H
