// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SESSION_SESSION_STREAM_H
#define RIWO_HTTP_EXP_SESSION_SESSION_STREAM_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/writer.h>

#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/capabilities.h>
#include <riwo/http_exp/version.h>

namespace riwo::http_exp { namespace detail {
class duplex_state;
} //namespace detail

class RIWO_HTTP_EXP_API session_stream
{
	RIWO_DISABLE_COPY_MOVE(session_stream)

public:
	using endpoint_t = asio::ip::tcp::endpoint;
	using duplex_state_ptr = std::shared_ptr<detail::duplex_state>;

	using response_handler_t = asio::any_completion_handler <
		void(error_code, response_head)
	>;
	using operation_handler_t = asio::any_completion_handler <
		void(error_code)
	>;
	using duplex_handler_t = asio::any_completion_handler <
		void(error_code, duplex_state_ptr)
	>;
	session_stream() = default;
	virtual ~session_stream() = default;

public:
	[[nodiscard]] virtual stream_capabilities capabilities() const noexcept = 0;
	[[nodiscard]] virtual request_head request() const = 0;

	[[nodiscard]] virtual std::shared_ptr<detail::body_reader_state> inbound_body() const noexcept = 0;
	[[nodiscard]] virtual std::shared_ptr<detail::body_writer_state> outbound_body() const noexcept = 0;

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

using session_stream_ptr = std::shared_ptr<session_stream>;

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<detail::duplex_state> make_body_duplex_state (
	protocol version, detail::body_reader_state_ptr reader,
	detail::body_writer_state_ptr writer, asio::ip::tcp::endpoint remote,
	asio::ip::tcp::endpoint local, std::shared_ptr<void> owner,
	std::function<void()> cancel, asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<detail::duplex_state> make_body_duplex_state (
	asio::any_io_executor executor, protocol version,
	detail::body_reader_state_ptr reader, detail::body_writer_state_ptr writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel
);

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SESSION_SESSION_STREAM_H
