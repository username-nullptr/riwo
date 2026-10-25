// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SESSION_SERVER_SESSION_H
#define RIWO_HTTP_EXP_SESSION_SERVER_SESSION_H

#include <riwo/http_exp/session/session_stream.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API server_session
{
public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;

	using accept_handler_t = asio::any_completion_handler <
		void(error_code, std::shared_ptr<session_stream>)
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

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SESSION_SERVER_SESSION_H
