// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_H

#include <riwo/websocket/detail/stream/receive_operations.h>
#include <riwo/websocket/detail/stream/receive_buffer.h>
#include <riwo/websocket/detail/stream/receive_engine_owner.h>
#include <riwo/core/async_expected.h>

namespace riwo::websocket::detail
{

enum class receive_failure_origin : uint8_t {
	none, transport, protocol, buffer,
};

struct receive_event_result
{
	optional<received_event> event {};
	error_code error {};

	receive_failure_origin failure_origin =
		receive_failure_origin::none;
};

class RIWO_WEBSOCKET_API receive_engine
{
	RIWO_DISABLE_COPY_MOVE(receive_engine)

public:
	using message_handler_t = asio::any_completion_handler<void(error_code,message)>;
	using frame_handler_t = asio::any_completion_handler<void(error_code,data_frame)>;
	using info_handler_t = asio::any_completion_handler<void(error_code,message_info)>;

	explicit receive_engine(receive_engine_owner &owner) noexcept;
	~receive_engine();

	void reset(role local_role, const stream_config &config,
		std::span<const extension> extensions, std::vector<std::byte> pending_data
	);
	[[nodiscard]] bool active() const noexcept;
	void set_active(bool value) noexcept;

	[[nodiscard]] receive_event_result next_event (
		receive_target target = receive_target::message
	) noexcept;

	[[nodiscard]] awaitable<receive_event_result> async_next_event (
		receive_target target = receive_target::message
	);
	[[nodiscard]] message read(error_code &error) noexcept;
	[[nodiscard]] data_frame read_frame(error_code &error) noexcept;

	template <typename Consumer>
	[[nodiscard]] message_info consume(Consumer &&consumer, error_code &error) noexcept;

	void async_read_message(message_handler_t completion);
	void async_read_frame(frame_handler_t completion);

	void start_close_receive() noexcept;

	template <typename Consumer>
	void async_consume(Consumer &&consumer, info_handler_t completion);

	void complete_read_waiter(error_code error,
		message value = {}, bool clear_slot = true
	) noexcept;

	void complete_frame_read_waiter(error_code error,
		data_frame value = {}, bool clear_slot = true
	) noexcept;

	void complete_consume_waiter(error_code error,
		message_info value = {}, bool clear_slot = true
	) noexcept;

private:
	struct close_receive_handler
	{
		std::shared_ptr<receive_engine_owner> owner;
		void operator()(const std::exception_ptr &exception,
			error_code error) const noexcept;
	};
	[[nodiscard]] message finish_read_error(error_code &error) noexcept;
	void cancel_read_waiter(uint64_t id, asio::cancellation_type type) noexcept;

private:
	receive_engine_owner &m_owner;
	receive_buffer m_buffer {};

	error_code m_read_error {};
	bool m_read_active = false;

	std::shared_ptr<read_wait_operation> m_read_waiter {};
	std::shared_ptr<frame_read_wait_operation> m_frame_read_waiter {};
	std::shared_ptr<consume_wait_operation> m_consume_waiter {};
	uint64_t m_next_read_waiter_id = 0;
};

} //namespace riwo::websocket::detail
# include <riwo/websocket/detail/stream/receive_engine_read.ipp>


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_H
