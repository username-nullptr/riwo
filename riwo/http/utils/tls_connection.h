// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_TLS_CONNECTION_H
#define RIWO_HTTP_UTILS_TLS_CONNECTION_H

#include <riwo/http/utils/connection.h>
#include <riwo/core/execution.h>

#if RIWO_OPENSSL_SUPPORT

namespace riwo::http
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_tls_connection : public basic_connection<Exec>
{
	RIWO_DISABLE_COPY_MOVE(basic_tls_connection)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using probe_state_t = connection_probe_state;

	using socket_t = asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,executor_t>>;
	using ptr_t = std::shared_ptr<basic_tls_connection>;

	explicit basic_tls_connection(socket_t &&socket);
	~basic_tls_connection() override;

public:
	sys_expected<> cancel() noexcept override;
	sys_expected<> close() noexcept override;

	sys_expected<> set_options(const tcp_socket_options &options) noexcept override;
	[[nodiscard]] sys_expected<tcp_socket_state> options() const noexcept override;

	[[nodiscard]] bool is_open() const noexcept override;
	[[nodiscard]] sys_expected<probe_state_t> probe() noexcept override;

	[[nodiscard]] endpoint remote_endpoint() const noexcept override;
	[[nodiscard]] endpoint local_endpoint() const noexcept override;

	[[nodiscard]] executor_t get_executor() noexcept override;

protected:
	[[nodiscard]] size_t read_some(mutable_buffer buffer, error_code &error) noexcept override;
	[[nodiscard]] size_t write_all(const const_buffer &buffer, error_code &error) noexcept override;
	[[nodiscard]] size_t write_all(std::span<const const_buffer> buffers, error_code &error) noexcept override;

protected:
	using io_handler_t = basic_connection<Exec>::io_handler_t;
	void co_read_some(mutable_buffer buffer, io_handler_t handler) noexcept override;
	void co_write_all(const_buffer buffer, io_handler_t handler) noexcept override;
	void co_write_all(std::span<const const_buffer> buffers, io_handler_t handler) noexcept override;

private:
	socket_t m_socket;
};

using tls_connection = basic_tls_connection<>;
using tls_connection_ptr = basic_tls_connection<>::ptr_t;

} //namespace riwo::http
#include <riwo/http/utils/detail/tls_connection.h>

#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_HTTP_UTILS_TLS_CONNECTION_H
