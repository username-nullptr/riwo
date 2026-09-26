// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_OWNER_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_OWNER_H

#include <riwo/websocket/detail/stream/send_operations.h>

namespace riwo::websocket::detail
{

class send_engine;

class RIWO_WEBSOCKET_API send_engine_owner
{
public:
	virtual ~send_engine_owner() = default;

	[[nodiscard]] virtual std::shared_ptr<send_engine_owner> send_owner() noexcept = 0;
	[[nodiscard]] virtual std::weak_ptr<send_engine_owner> weak_send_owner() noexcept = 0;

	[[nodiscard]] virtual send_engine &send_side() noexcept = 0;
	[[nodiscard]] virtual asio::any_io_executor send_executor() const noexcept = 0;

	virtual void start_transport_write(prepared_frame frame,
		wire_frame_kind kind, std::shared_ptr<send_operation> operation
	) noexcept = 0;

	virtual void handle_send_failure(error_code error) noexcept = 0;
	virtual void handle_wire_frame_sent(wire_frame_kind kind) noexcept = 0;

	[[nodiscard]] virtual size_t write_prepared (
		const prepared_frame &frame, error_code &error
	) noexcept = 0;

	[[nodiscard]] virtual error_code write_state_error() const noexcept = 0;
	[[nodiscard]] virtual error_code frame_write_state_error() const noexcept = 0;
	[[nodiscard]] virtual error_code protocol_failure_error(error_code fallback) const noexcept = 0;

	[[nodiscard]] virtual bool send_transport_ready() const noexcept = 0;
	[[nodiscard]] virtual bool protocol_failure_active() const noexcept = 0;
};

} //namespace riwo::websocket::detail

#endif //RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_OWNER_H
