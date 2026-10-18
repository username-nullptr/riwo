// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/writer.h>
#include <riwo/http_exp/message/request_head.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API stream
{
public:
	using response_handler = asio::any_completion_handler<void(
		error_code, response_head)>;
	using operation_handler = asio::any_completion_handler<void(error_code)>;

	virtual ~stream() = default;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual stream_capabilities capabilities() const noexcept;
	[[nodiscard]] virtual std::shared_ptr<body_reader_state>
	inbound_body() const noexcept;
	[[nodiscard]] virtual std::shared_ptr<body_writer_state>
	outbound_body() const noexcept;
	[[nodiscard]] virtual response_head wait_response_sync(error_code &error) noexcept;
	virtual void wait_response(response_handler handler);
	virtual void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept;
	virtual void respond(response_head head, body_spec spec,
		operation_handler handler);
	virtual void inform_sync(response_head head, error_code &error) noexcept;
	virtual void inform(response_head head, operation_handler handler);
	virtual void cancel() noexcept = 0;
};

class RIWO_HTTP_EXP_API client_session
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
	[[nodiscard]] virtual std::shared_ptr<stream> open_sync(request_head head,
		body_spec spec, error_code &error) noexcept;
	virtual void open(request_head head, body_spec spec, open_handler handler) = 0;
	virtual void graceful_shutdown(shutdown_handler handler) = 0;
};

class RIWO_HTTP_EXP_API server_session
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
