// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_ACCEPTOR_WRAP_H
#define RIWO_HTTP_SERVER_ACCEPTOR_WRAP_H

#include <riwo/http/utils/tcp_connection.h>
#include <riwo/http/utils/tls_connection.h>
#include <riwo/coro.h>

namespace riwo::http
{

template <concepts::any_exec_stream Stream = asio::ip::tcp::socket>
class basic_acceptor_wrap;

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_acceptor_wrap
	<asio::basic_stream_socket<asio::ip::tcp,Exec>>
{
	RIWO_DISABLE_COPY(basic_acceptor_wrap)

	template <concepts::any_exec_stream>
	friend class basic_acceptor_wrap;

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using protocol_t = asio::ip::tcp;
	using socket_t = asio::basic_stream_socket<protocol_t,executor_t>;
	using acceptor_t = asio::basic_socket_acceptor<protocol_t,executor_t>;

public:
	basic_acceptor_wrap(acceptor_t &&acceptor);
	~basic_acceptor_wrap() = default;

	basic_acceptor_wrap(basic_acceptor_wrap&&) noexcept = default;
	basic_acceptor_wrap &operator=(basic_acceptor_wrap&&) noexcept = default;

	void accept (
		core_concepts::match_sched<executor_t> auto &&service_exec,
		std::function<void(connection_ptr)> callback
	);
	[[nodiscard]] executor_t get_executor() noexcept;
	[[nodiscard]] const acceptor_t &acceptor() const noexcept;
	[[nodiscard]] acceptor_t &acceptor() noexcept;

protected:
	acceptor_t m_acceptor;
};

#if RIWO_OPENSSL_SUPPORT

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_acceptor_wrap
	<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>>
{
	RIWO_DISABLE_COPY(basic_acceptor_wrap)

	template <concepts::any_exec_stream>
	friend class basic_acceptor_wrap;

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using protocol_t = asio::ip::tcp;
	using socket_t = asio::ssl::stream<asio::basic_stream_socket<protocol_t,executor_t>>;

	using acceptor_t = asio::basic_socket_acceptor<protocol_t,executor_t>;
	using context_t = asio::ssl::context;

public:
	basic_acceptor_wrap(acceptor_t &&acceptor, context_t &ctx);
	~basic_acceptor_wrap() = default;

	basic_acceptor_wrap(basic_acceptor_wrap&&) noexcept = default;
	basic_acceptor_wrap &operator=(basic_acceptor_wrap&&) noexcept = default;

	void accept (
		core_concepts::match_sched<executor_t> auto &&service_exec,
		std::function<void(connection_ptr)> callback,
		std::chrono::milliseconds handshake_timeout = std::chrono::milliseconds(5000)
	);
	[[nodiscard]] executor_t get_executor() noexcept;
	[[nodiscard]] const acceptor_t &acceptor() const noexcept;
	[[nodiscard]] acceptor_t &acceptor() noexcept;

protected:
	acceptor_t m_acceptor;
	context_t *m_ctx;
};

#endif //RIWO_OPENSSL_SUPPORT

using acceptor_wrap = basic_acceptor_wrap<>;

} //namespace riwo::http
#include <riwo/http/server/detail/acceptor_wrap.h>


#endif //RIWO_HTTP_SERVER_ACCEPTOR_WRAP_H
