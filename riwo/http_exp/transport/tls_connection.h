// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_TLS_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_TLS_CONNECTION_H

#include <riwo/http_exp/transport/connection.h>

#if RIWO_OPENSSL_SUPPORT

namespace riwo::http_exp
{

using tls_socket = asio::ssl::stream<asio::ip::tcp::socket>;

class RIWO_HTTP_EXP_API tls_connection final : public connection,
	public std::enable_shared_from_this<tls_connection>
{
	RIWO_DISABLE_COPY_MOVE(tls_connection)

public:
	explicit tls_connection(tls_socket socket);
	~tls_connection() override;

	[[nodiscard]] executor_type get_executor() const noexcept override;
	void read_some(mutable_buffer buffer, io_handler handler) override;
	void write(const_buffer buffer, io_handler handler) override;
	void close(close_handler handler) override;
	void cancel() noexcept override;
	[[nodiscard]] bool is_open() const noexcept override;

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override;

	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override;

	[[nodiscard]] tls_socket &socket() noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::http_exp

#endif //RIWO_OPENSSL_SUPPORT

#endif //RIWO_HTTP_EXP_TRANSPORT_TLS_CONNECTION_H
