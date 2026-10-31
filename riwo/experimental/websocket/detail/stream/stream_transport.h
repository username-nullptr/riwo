// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_WEBSOCKET_DETAIL_STREAM_STREAM_TRANSPORT_H
#define RIWO_EXP_WEBSOCKET_DETAIL_STREAM_STREAM_TRANSPORT_H

#include <riwo/experimental/websocket/detail/stream/send_engine_owner.h>
#include <riwo/experimental/http/protocol/duplex_channel.h>
#include <riwo/core/async_expected.h>

namespace riwo::experimental::websocket::detail
{

class RIWO_EXP_WEBSOCKET_API stream_transport final
{
	RIWO_DISABLE_COPY_MOVE(stream_transport)

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;

	stream_transport() noexcept;
	~stream_transport();

	void reset(experimental::http::duplex_channel_io_ptr connection) noexcept;

	[[nodiscard]] awaitable<std::tuple<error_code,size_t>>
	async_read(std::shared_ptr<std::vector<std::byte>> storage);

	void start_write(const send_engine_owner_ptr &owner,
		const prepared_frame &frame, wire_frame_kind kind,
		const send_operation_ptr &operation
	) noexcept;

private:
	experimental::http::duplex_channel_io_ptr m_connection {};
};

} //namespace riwo::experimental::websocket::detail


#endif //RIWO_EXP_WEBSOCKET_DETAIL_STREAM_STREAM_TRANSPORT_H
