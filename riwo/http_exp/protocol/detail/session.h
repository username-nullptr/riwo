// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/writer.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API stream
{
public:
	using endpoint_type = asio::ip::tcp::endpoint;
	using response_handler = asio::any_completion_handler<void(
		error_code, response_head)>;
	using operation_handler = asio::any_completion_handler<void(error_code)>;
	using duplex_handler = asio::any_completion_handler<void(
		error_code, std::shared_ptr<duplex_state>)>;

	virtual ~stream() = default;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual protocol response_protocol() const noexcept {
		return selected_protocol();
	}
	[[nodiscard]] virtual stream_capabilities capabilities() const noexcept;
	[[nodiscard]] virtual request_head request() const;
	[[nodiscard]] virtual std::shared_ptr<body_reader_state>
	inbound_body() const noexcept;
	[[nodiscard]] virtual std::shared_ptr<body_writer_state>
	outbound_body() const noexcept;
	[[nodiscard]] virtual response_head wait_response_sync(error_code &error) noexcept;
	[[nodiscard]] virtual endpoint_type remote_endpoint(
		error_code &error) const noexcept;
	[[nodiscard]] virtual endpoint_type local_endpoint(
		error_code &error) const noexcept;
	virtual void wait_response(response_handler handler);
	virtual void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept;
	virtual void respond(response_head head, body_spec spec,
		operation_handler handler);
	virtual void inform_sync(response_head head, error_code &error) noexcept;
	virtual void inform(response_head head, operation_handler handler);
	[[nodiscard]] virtual std::shared_ptr<duplex_state> open_duplex_sync(
		optional<response_head> response, error_code &error) noexcept;
	virtual void open_duplex(optional<response_head> response,
		duplex_handler handler);
	[[nodiscard]] virtual bool reusable() const noexcept { return false; }
	[[nodiscard]] virtual request_delivery delivery() const noexcept {
		return request_delivery::possibly_processed;
	}
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
	// Capacity is reserved and released on the Session strand. Pools use these
	// hooks to avoid a check-then-open race; the default keeps custom test and
	// application sessions source-compatible.
	[[nodiscard]] virtual bool try_reserve_stream() noexcept {
		return available_stream_capacity() != 0;
	}
	virtual void release_stream(bool reusable) noexcept {
		ignore_unused(reusable);
	}
	// The pool uses this observation to return a Session to the idle set as
	// soon as the response boundary is known, without waiting for the final
	// application handle to be destroyed.
	virtual void set_release_handler(std::function<void(bool)> handler) {
		ignore_unused(handler);
	}
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

[[nodiscard]] RIWO_HTTP_EXP_API std::shared_ptr<duplex_state>
make_body_duplex_state(asio::any_io_executor executor, protocol version,
	std::shared_ptr<body_reader_state> reader,
	std::shared_ptr<body_writer_state> writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel);

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
