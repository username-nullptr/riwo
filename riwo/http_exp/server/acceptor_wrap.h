// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_ACCEPTOR_WRAP_H
#define RIWO_HTTP_EXP_SERVER_ACCEPTOR_WRAP_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

/**
 * Listener ownership wrapper retained for source-shape compatibility with the
 * stable server.  Protocol detection and Session construction are attached to
 * this boundary in later stages; no HTTP/1 connection object is exposed.
 */
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_acceptor_wrap
{
	RIWO_DISABLE_COPY(basic_acceptor_wrap)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using protocol_t = asio::ip::tcp;
	using acceptor_t = asio::basic_socket_acceptor<protocol_t,executor_t>;

	basic_acceptor_wrap(basic_acceptor_wrap &&) noexcept = default;
	basic_acceptor_wrap &operator=(basic_acceptor_wrap &&) noexcept = default;

	basic_acceptor_wrap()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	// Intentionally implicit: `server(std::move(tcp_acceptor))` remains valid.
	basic_acceptor_wrap(acceptor_t &&acceptor);

	[[nodiscard]] executor_t get_executor() noexcept;

	[[nodiscard]] const acceptor_t &acceptor() const noexcept;

	[[nodiscard]] acceptor_t &acceptor() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using acceptor_wrap = basic_acceptor_wrap<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/server/detail/acceptor_wrap.h>

#endif //RIWO_HTTP_EXP_SERVER_ACCEPTOR_WRAP_H
