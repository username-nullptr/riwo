// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/writer.h>

#include <riwo/http_exp/protocol/capabilities.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol/version.h>
#include <typeinfo>

namespace riwo::http_exp::detail
{

class duplex_state;

class RIWO_HTTP_EXP_API stream
{
	RIWO_DISABLE_COPY_MOVE(stream)

public:
	using endpoint_t = asio::ip::tcp::endpoint;
	using duplex_state_ptr = std::shared_ptr<duplex_state>;

	using response_handler_t = asio::any_completion_handler <
		void(error_code, response_head)
	>;
	using operation_handler_t = asio::any_completion_handler <
		void(error_code)
	>;
	using duplex_handler_t = asio::any_completion_handler <
		void(error_code, duplex_state_ptr)
	>;

	stream() = default;
	virtual ~stream() = default;

public:
	[[nodiscard]] virtual stream_capabilities capabilities() const noexcept = 0;
	[[nodiscard]] virtual request_head request() const = 0;

	[[nodiscard]] virtual std::shared_ptr<body_reader_state> inbound_body() const noexcept = 0;
	[[nodiscard]] virtual std::shared_ptr<body_writer_state> outbound_body() const noexcept = 0;

	[[nodiscard]] virtual response_head wait_response_sync(error_code &error) noexcept;
	virtual void wait_response(response_handler_t handler);

	virtual void respond_sync(const response_head &head, const body_spec &spec, error_code &error) noexcept;
	virtual void respond(const response_head &head, const body_spec &spec, operation_handler_t handler);

	virtual void inform_sync(const response_head &head, error_code &error) noexcept;
	virtual void inform(const response_head &head, operation_handler_t handler);

	[[nodiscard]] virtual duplex_state_ptr open_duplex_sync (
		const optional<response_head> &response, error_code &error
	) noexcept;

	virtual void open_duplex (
		const optional<response_head> &response, duplex_handler_t handler
	);
	virtual void cancel() noexcept = 0;

public:
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual protocol response_protocol() const noexcept;

	[[nodiscard]] virtual endpoint_t remote_endpoint(error_code &error) const noexcept;
	[[nodiscard]] virtual endpoint_t local_endpoint(error_code &error) const noexcept;

	[[nodiscard]] virtual bool reusable() const noexcept;
	[[nodiscard]] virtual request_delivery delivery() const noexcept;

	// Protocol-specific diagnostic handles are opt-in. Non-matching protocols
	// intentionally return nullptr.
	[[nodiscard]] virtual void *protocol_extension(const std::type_info &type) noexcept;
};

using stream_ptr = std::shared_ptr<stream>;

class RIWO_HTTP_EXP_API client_session
{
	RIWO_DISABLE_COPY_MOVE(client_session)

public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;

	using open_handler_t = asio::any_completion_handler <
		void(error_code, std::shared_ptr<stream>)
	>;
	using shutdown_handler_t = asio::any_completion_handler <
		void(error_code)
	>;
	client_session() = default;
	virtual ~client_session() = default;

public:
	// Capacity is reserved and released on the Session strand. Pools use these
	// hooks to avoid a check-then-open race; the default keeps custom test and
	// application sessions source-compatible.
	[[nodiscard]] virtual bool try_reserve_stream() noexcept;
	virtual void release_stream(bool reusable) noexcept;

	// The pool uses this observation to return a Session to the idle set as
	// soon as the response boundary is known, without waiting for the final
	// application handle to be destroyed.
	virtual void set_release_handler(std::function<void(bool)> handler);

	[[nodiscard]] virtual stream_ptr open_sync (
		const request_head &head, const body_spec &spec, error_code &error
	) noexcept;

	virtual void open(request_head head, body_spec spec, open_handler_t handler) = 0;
	virtual void graceful_shutdown(shutdown_handler_t handler) = 0;

public:
	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;

	[[nodiscard]] virtual size_t available_stream_capacity() const noexcept = 0;
	[[nodiscard]] virtual bool draining() const noexcept = 0;
	[[nodiscard]] virtual bool closed() const noexcept = 0;
};

using client_session_ptr = std::shared_ptr<client_session>;

class RIWO_HTTP_EXP_API server_session
{
public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;

	using accept_handler_t = asio::any_completion_handler <
		void(error_code, std::shared_ptr<stream>)
	>;
	using shutdown_handler_t = asio::any_completion_handler <
		void(error_code)
	>;
	server_session() = default;
	virtual ~server_session() = default;

public:
	virtual void accept(accept_handler_t handler) = 0;
	virtual void graceful_shutdown(shutdown_handler_t handler) = 0;

	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;

	[[nodiscard]] virtual bool draining() const noexcept = 0;
	[[nodiscard]] virtual bool closed() const noexcept = 0;
};

using server_session_ptr = std::shared_ptr<server_session>;

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<duplex_state>make_body_duplex_state(protocol version,
	body_reader_state_ptr reader, body_writer_state_ptr writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel,
	asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<duplex_state> make_body_duplex_state (
	asio::any_io_executor executor, protocol version,
	body_reader_state_ptr reader, body_writer_state_ptr writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel
);

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_SESSION_H
