// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H

#include <riwo/http_exp/global.h>
#include <chrono>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API connection
{
	RIWO_DISABLE_COPY_MOVE(connection)

public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;
	using io_handler_t = asio::any_completion_handler<void(error_code, size_t)>;
	using close_handler_t = asio::any_completion_handler<void(error_code)>;
	using endpoint_t = asio::ip::tcp::endpoint;
	using timeout_t = std::chrono::milliseconds;

	connection();
	virtual ~connection();
	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	virtual void read_some(mutable_buffer buffer, io_handler_t handler) = 0;
	virtual void write(const_buffer buffer, io_handler_t handler) = 0;
	virtual void close(close_handler_t handler) = 0;
	virtual void cancel() noexcept = 0;
	virtual void set_read_timeout(optional<timeout_t> value);
	virtual void set_write_timeout(optional<timeout_t> value);

	[[nodiscard]] virtual bool is_open() const noexcept;

	// Synchronous completion is used by the existing synchronous public API and
	// by the pool's non-waiting fast path. Custom transports may keep the
	// asynchronous interface only; the default reports that explicitly.
	[[nodiscard]] virtual size_t read_some_sync(mutable_buffer,
		error_code &error) noexcept;

	[[nodiscard]] virtual size_t write_sync(const_buffer,
		error_code &error) noexcept;

	virtual void connect_sync(std::string_view host, uint16_t port,
		error_code &error) noexcept;

	virtual void connect_async(std::string_view host, uint16_t port,
		close_handler_t handler);

	[[nodiscard]] virtual endpoint_t remote_endpoint(
		error_code &error) const noexcept;

	[[nodiscard]] virtual endpoint_t local_endpoint(
		error_code &error) const noexcept;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H
