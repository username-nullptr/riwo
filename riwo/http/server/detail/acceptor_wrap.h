// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_ACCEPTOR_WRAP_H
#define RIWO_HTTP_SERVER_DETAIL_ACCEPTOR_WRAP_H

namespace riwo::http
{

template <core_concepts::exec Exec>
basic_acceptor_wrap<asio::basic_stream_socket<asio::ip::tcp,Exec>>::
basic_acceptor_wrap(acceptor_t &&acceptor) :
	m_acceptor(std::move(acceptor))
{

}

template <core_concepts::exec Exec>
void basic_acceptor_wrap<asio::basic_stream_socket<asio::ip::tcp,Exec>>::accept
(core_concepts::match_sched<executor_t> auto &&service_exec, std::function<void(connection_ptr)> callback)
{
	auto exec = get_executor_helper(
		std::forward<decltype(service_exec)>(service_exec)
	);
	m_acceptor.async_accept(exec,
	[this, exec, accept_callback = std::move(callback)](const error_code &error, auto socket) mutable
	{
		if( error )
		{
			accept_callback(nullptr);
			if( m_acceptor.is_open() )
				accept(exec, std::move(accept_callback));
			return ;
		}
		// Keep an accept pending before handing the connection to the server.
		// This also keeps TCP and TLS accept behavior consistent.
		accept(exec, accept_callback);
		accept_callback(std::make_shared<basic_tcp_connection<executor_t>>(
			socket_t(std::move(socket))
		));
	});
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::basic_stream_socket<asio::ip::tcp,Exec>>::
get_executor() noexcept -> executor_t
{
	return m_acceptor.get_executor();
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::basic_stream_socket<asio::ip::tcp,Exec>>::
acceptor() const noexcept -> const acceptor_t&
{
	return m_acceptor;
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::basic_stream_socket<asio::ip::tcp,Exec>>::
acceptor() noexcept -> acceptor_t&
{
	return m_acceptor;
}

#if RIWO_OPENSSL_SUPPORT

template <core_concepts::exec Exec>
basic_acceptor_wrap<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>::
basic_acceptor_wrap(acceptor_t &&acceptor, asio::ssl::context &ctx) :
	m_acceptor(std::move(acceptor)), m_ctx(&ctx)
{

}

template <core_concepts::exec Exec>
void basic_acceptor_wrap<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>::accept
(core_concepts::match_sched<executor_t> auto &&service_exec, std::function<void(connection_ptr)> callback_arg,
	std::chrono::milliseconds handshake_timeout)
{
	using namespace std::chrono_literals;
	if( handshake_timeout <= 0ms )
		handshake_timeout = 1ms;

	auto exec = get_executor_helper (
		std::forward<decltype(service_exec)>(service_exec)
	);
	m_acceptor.async_accept(exec,
	[this, exec, callback = std::move(callback_arg), handshake_timeout]
	(const error_code &error, auto tcp_socket) mutable
	{
		if( error )
		{
			callback(nullptr);
			if( m_acceptor.is_open() )
				accept(exec, std::move(callback), handshake_timeout);
			return ;
		}
		auto ssl_socket = std::make_shared<socket_t>(
			socket_t(std::move(tcp_socket), *m_ctx)
		);
		auto strand = asio::make_strand(ssl_socket->get_executor());
		auto timer = std::make_shared<asio::steady_timer>(strand);
		auto completed = std::make_shared<std::atomic_bool>(false);

		timer->expires_after(handshake_timeout);
		timer->async_wait(asio::bind_executor(strand,
		[ssl_socket, timer, completed, callback](const error_code &timer_error) mutable
		{
			if( timer_error or completed->exchange(true) )
				return ;

			error_code ignored {};
			ssl_socket->next_layer().cancel(ignored);
			ssl_socket->next_layer().close(ignored);
			callback(nullptr);
		}));
		ssl_socket->async_handshake(asio::ssl::stream_base::server,
		asio::bind_executor(strand,
		[ssl_socket, timer, completed, callback](const error_code &handshake_error) mutable
		{
			if( completed->exchange(true) )
				return ;
			try {
				ignore_unused(timer->cancel());
			}
			catch(...) {}

			error_code ignored {};
			if( handshake_error )
			{
				ssl_socket->next_layer().close(ignored);
				callback(nullptr);
				return ;
			}
			callback(std::make_shared<basic_tls_connection<executor_t>>(
				std::move(*ssl_socket)
			));
		}));
		// The next TCP accept is started immediately. TLS handshakes already in
		// progress therefore cannot serialize admission of later clients.
		accept(exec, std::move(callback), handshake_timeout);
	});
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>::
get_executor() noexcept -> executor_t
{
	return m_acceptor.get_executor();
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>::
acceptor() const noexcept -> const acceptor_t&
{
	return m_acceptor;
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>::
acceptor() noexcept -> acceptor_t&
{
	return m_acceptor;
}

#endif //RIWO_OPENSSL_SUPPORT

} //namespace riwo::http


#endif //RIWO_HTTP_SERVER_DETAIL_ACCEPTOR_WRAP_H
