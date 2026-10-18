// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTION_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp::detail
{

class connection
{
	RIWO_DISABLE_COPY_MOVE(connection)

public:
	using executor_type = asio::any_io_executor;
	using io_handler = asio::any_completion_handler<void(error_code, size_t)>;
	using close_handler = asio::any_completion_handler<void(error_code)>;

	connection() = default;
	virtual ~connection() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	virtual void read_some(mutable_buffer buffer, io_handler handler) = 0;
	virtual void write(const_buffer buffer, io_handler handler) = 0;
	virtual void close(close_handler handler) = 0;
	virtual void cancel() noexcept = 0;
};

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTION_H
