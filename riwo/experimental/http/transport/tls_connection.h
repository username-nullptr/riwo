// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TRANSPORT_TLS_CONNECTION_H
#define RIWO_EXP_HTTP_TRANSPORT_TLS_CONNECTION_H

#include <riwo/experimental/http/transport/connection.h>
#if RIWO_OPENSSL_SUPPORT

namespace riwo::experimental::http
{

using tls_socket_t = asio::ssl::stream<asio::ip::tcp::socket>;

class RIWO_DECL_HIDDEN tls_connection final : public connection,
	public std::enable_shared_from_this<tls_connection>
{
	RIWO_DISABLE_COPY_MOVE(tls_connection)

public:
	explicit tls_connection(tls_socket_t socket);
	~tls_connection() override;

public:
	void read_some(mutable_buffer buffer, io_handler_t handler) override;
	void write(const_buffer buffer, io_handler_t handler) override;
	using connection::write;

	[[nodiscard]] size_t read_some_sync(const mutable_buffer &buffer, error_code &error) noexcept override;
	[[nodiscard]] size_t write_sync(const const_buffer &buffer, error_code &error) noexcept override;

	void close(close_handler_t handler) override;
	void cancel() noexcept override;

public:
	void set_read_timeout(optional<timeout_t> value) override;
	void set_write_timeout(optional<timeout_t> value) override;

	[[nodiscard]] executor_t get_executor() const noexcept override;
	[[nodiscard]] bool is_open() const noexcept override;

	[[nodiscard]] endpoint_t remote_endpoint(error_code &error) const noexcept override;
	[[nodiscard]] endpoint_t local_endpoint(error_code &error) const noexcept override;

	[[nodiscard]] tls_socket_t &socket() noexcept;

protected:
	void write_span(std::span<const const_buffer> buffers, io_handler_t handler) override;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

using tls_connection_ptr = std::shared_ptr<tls_connection>;

} //namespace riwo::experimental::http

#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_EXP_HTTP_TRANSPORT_TLS_CONNECTION_H
