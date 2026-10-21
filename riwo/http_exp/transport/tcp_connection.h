// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_TCP_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_TCP_CONNECTION_H

#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API tcp_connection final : public connection,
	public std::enable_shared_from_this<tcp_connection>
{
	RIWO_DISABLE_COPY_MOVE(tcp_connection)

public:
	explicit tcp_connection(
		asio::any_io_executor executor = riwo::get_executor());
	explicit tcp_connection(asio::ip::tcp::socket socket);
	~tcp_connection() override;

	[[nodiscard]] executor_t get_executor() const noexcept override;
	void read_some(mutable_buffer buffer, io_handler_t handler) override;
	void write(const_buffer buffer, io_handler_t handler) override;
	void close(close_handler_t handler) override;
	void cancel() noexcept override;
	void set_read_timeout(optional<timeout_t> value) override;
	void set_write_timeout(optional<timeout_t> value) override;
	[[nodiscard]] bool is_open() const noexcept override;

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override;
	void connect_sync(std::string_view host, uint16_t port,
		error_code &error) noexcept override;
	void connect_async(std::string_view host, uint16_t port,
		close_handler_t handler) override;

	[[nodiscard]] endpoint_t remote_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] endpoint_t local_endpoint(
		error_code &error) const noexcept override;

	[[nodiscard]] asio::ip::tcp::socket &socket() noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_TCP_CONNECTION_H
