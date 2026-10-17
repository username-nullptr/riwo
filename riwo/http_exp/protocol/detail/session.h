// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H

#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/message/detail/request_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp::detail
{

class stream
{
public:
	virtual ~stream() = default;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	virtual void cancel() noexcept = 0;
};

class client_session
{
public:
	using executor_type = asio::any_io_executor;
	using open_handler = asio::any_completion_handler<void(
		error_code, std::shared_ptr<stream>)>;
	using shutdown_handler = asio::any_completion_handler<void(error_code)>;

	virtual ~client_session() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual size_t available_stream_capacity() const noexcept = 0;
	[[nodiscard]] virtual bool draining() const noexcept = 0;
	[[nodiscard]] virtual bool closed() const noexcept = 0;
	virtual void open(request_head head, body_spec spec, open_handler handler) = 0;
	virtual void graceful_shutdown(shutdown_handler handler) = 0;
};

class server_session
{
public:
	using executor_type = asio::any_io_executor;
	using accept_handler = asio::any_completion_handler<void(
		error_code, std::shared_ptr<stream>)>;
	using shutdown_handler = asio::any_completion_handler<void(error_code)>;

	virtual ~server_session() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual bool draining() const noexcept = 0;
	[[nodiscard]] virtual bool closed() const noexcept = 0;
	virtual void accept(accept_handler handler) = 0;
	virtual void graceful_shutdown(shutdown_handler handler) = 0;
};

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
