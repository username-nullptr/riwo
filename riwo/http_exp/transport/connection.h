// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

class connection
{
	RIWO_DISABLE_COPY_MOVE(connection)

public:
	using executor_type = asio::any_io_executor;
	using io_handler = asio::any_completion_handler<void(error_code, size_t)>;
	using close_handler = asio::any_completion_handler<void(error_code)>;
	using endpoint_type = asio::ip::tcp::endpoint;

	connection() = default;
	virtual ~connection() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	virtual void read_some(mutable_buffer buffer, io_handler handler) = 0;
	virtual void write(const_buffer buffer, io_handler handler) = 0;
	virtual void close(close_handler handler) = 0;
	virtual void cancel() noexcept = 0;

	[[nodiscard]] virtual bool is_open() const noexcept {
		return true;
	}

	// Synchronous completion is used by the existing synchronous public API and
	// by the pool's non-waiting fast path. Custom transports may keep the
	// asynchronous interface only; the default reports that explicitly.
	[[nodiscard]] virtual size_t read_some_sync(mutable_buffer,
		error_code &error) noexcept
	{
		error = make_error_code(errc::operation_not_supported);
		return 0;
	}

	[[nodiscard]] virtual size_t write_sync(const_buffer,
		error_code &error) noexcept
	{
		error = make_error_code(errc::operation_not_supported);
		return 0;
	}

	virtual void connect_sync(std::string_view host, uint16_t port,
		error_code &error) noexcept
	{
		ignore_unused(host);
		ignore_unused(port);
		error = make_error_code(errc::operation_not_supported);
	}

	virtual void connect_async(std::string_view host, uint16_t port,
		close_handler handler)
	{
		ignore_unused(host);
		ignore_unused(port);
		std::move(handler)(make_error_code(errc::operation_not_supported));
	}

	[[nodiscard]] virtual endpoint_type remote_endpoint(
		error_code &error) const noexcept
	{
		error = make_error_code(errc::operation_not_supported);
		return {};
	}

	[[nodiscard]] virtual endpoint_type local_endpoint(
		error_code &error) const noexcept
	{
		error = make_error_code(errc::operation_not_supported);
		return {};
	}
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_CONNECTION_H
