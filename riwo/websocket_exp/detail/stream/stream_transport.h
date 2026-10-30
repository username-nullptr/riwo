// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H
#define RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H

#include <riwo/websocket_exp/detail/stream/send_engine_owner.h>
#include <riwo/core/async_expected.h>

namespace riwo::websocket_exp::detail
{

class RIWO_WEBSOCKET_EXP_API stream_transport final
{
	RIWO_DISABLE_COPY_MOVE(stream_transport)

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using read_fn_t = void (*)(void*, const mutable_buffer&, io_handler_t);
	using write_fn_t = void (*)(void*, std::span<const const_buffer>, io_handler_t);

	stream_transport() noexcept;
	~stream_transport();

	void reset(std::shared_ptr<void> connection,
		read_fn_t read_fn, write_fn_t write_fn
	) noexcept;

	[[nodiscard]] awaitable<std::tuple<error_code,size_t>>
	async_read(std::shared_ptr<std::vector<std::byte>> storage);

	void start_write(const send_engine_owner_ptr &owner,
		prepared_frame frame, wire_frame_kind kind, const send_operation_ptr &operation
	) noexcept;

private:
	std::shared_ptr<void> m_connection {};
	read_fn_t m_read_fn = nullptr;
	write_fn_t m_write_fn = nullptr;
};

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_STREAM_STREAM_TRANSPORT_H
