// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SESSION_CLIENT_SESSION_H
#define RIWO_HTTP_EXP_SESSION_CLIENT_SESSION_H

#include <riwo/http_exp/session/session_stream.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API client_session
{
	RIWO_DISABLE_COPY_MOVE(client_session)

public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;

	using open_handler_t = asio::any_completion_handler <
		void(error_code, std::shared_ptr<session_stream>)
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

	[[nodiscard]] virtual session_stream_ptr open_sync (
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

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SESSION_CLIENT_SESSION_H
