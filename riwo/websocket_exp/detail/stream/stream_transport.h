// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H
#define RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H

#include <riwo/websocket_exp/detail/stream/send_engine_owner.h>
#include <riwo/http_exp/protocol/duplex_channel.h>
#include <riwo/core/async_expected.h>

namespace riwo::websocket_exp::detail
{

class RIWO_WEBSOCKET_EXP_API stream_transport final
{
	RIWO_DISABLE_COPY_MOVE(stream_transport)

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;

	stream_transport() noexcept;
	~stream_transport();

	void reset(http_exp::detail::duplex_state_ptr connection) noexcept;

	[[nodiscard]] awaitable<std::tuple<error_code,size_t>>
	async_read(std::shared_ptr<std::vector<std::byte>> storage);

	void start_write(const send_engine_owner_ptr &owner,
		const prepared_frame &frame, wire_frame_kind kind,
		const send_operation_ptr &operation
	) noexcept;

private:
	http_exp::detail::duplex_state_ptr m_connection {};
};

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H
