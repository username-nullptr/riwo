// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_H

#include <riwo/websocket/detail/stream/send_engine_owner.h>

namespace riwo::websocket::detail
{

// Owns the complete outbound state machine. Owner remains the single lifetime
// anchor and supplies transport/lifecycle hooks; the engine itself is a value
// member and never participates in shared ownership.
class RIWO_WEBSOCKET_API send_engine
{
	RIWO_DISABLE_COPY_MOVE(send_engine)

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using void_handler_t = asio::any_completion_handler<void(error_code)>;

	explicit send_engine(send_engine_owner &owner) noexcept;
	~send_engine();

	void reset(role local_role, const stream_config &config,
		std::span<const extension> extensions
	) noexcept;

	[[nodiscard]] sys_expected<prepared_frame> prepare_control (
		opcode op, const const_buffer &payload, bool borrow_payload = false
	) const noexcept;

	[[nodiscard]] sys_expected<prepared_frame> prepare_close(const close_frame &frame) const noexcept;

	[[nodiscard]] sys_expected<std::vector<prepared_frame>> prepare_message (
		message_type type, std::span<const const_buffer> buffers, write_options options = {}
	) const noexcept;

	[[nodiscard]] sys_expected<prepared_data_frame> prepare_data_frame (
		message_type type, const const_buffer &payload, bool continuation, bool fin
	) const noexcept;

	[[nodiscard]] bool busy() const noexcept;
	[[nodiscard]] bool wire_write_active() const noexcept;
	[[nodiscard]] bool current_data_active() const noexcept;
	[[nodiscard]] bool data_busy() const noexcept;
	[[nodiscard]] bool ready_for_sync_protocol_write() const noexcept;

	void fail_current_if_idle(error_code error) noexcept;
	void fail_queued_controls(error_code error);
	void fail_queued_writes(error_code error);

	void clear_auto_pong() noexcept;
	void clear_local_close() noexcept;
	void clear_close_response() noexcept;
	void clear_protocol_close() noexcept;
	void clear_protocol_frames() noexcept;

	void queue_local_close(prepared_frame frame);
	void queue_protocol_close(prepared_frame frame);
	[[nodiscard]] bool has_local_close() const noexcept;

	[[nodiscard]] sys_expected<> queue_close_response(const std::vector<std::byte> &payload) noexcept;
	[[nodiscard]] sys_expected<> queue_auto_pong(const std::vector<std::byte> &payload) noexcept;
	void schedule();

	void async_write_message(message_type type, std::span<const const_buffer> buffers,
		write_options options, std::shared_ptr<std::vector<std::byte>> payload_owner,
		io_handler_t completion
	);
	void async_write_data_frame(message_type type, const const_buffer &payload,
		bool continuation, bool fin, std::shared_ptr<std::vector<std::byte>> payload_owner,
		io_handler_t completion
	);

	[[nodiscard]] size_t write_data_frame (
		message_type type, const const_buffer &payload,
		bool continuation, bool fin, error_code &error
	) noexcept;

	void async_write_control (
		opcode op, const const_buffer &payload, io_handler_t completion
	);
	[[nodiscard]] size_t write_control (
		opcode op, const const_buffer &payload, error_code &error
	) noexcept;

	void wait_written(error_code &error) noexcept;
	void async_wait_written(void_handler_t completion);

	// Transport is owned by stream::impl.  The engine only records the active
	// frame and consumes its completion to advance the outbound state machine.
	void complete_wire_frame(const prepared_frame &frame, wire_frame_kind kind,
		const std::shared_ptr<send_operation> &operation, error_code error, size_t wire_size
	) noexcept;

private:
	[[nodiscard]] bool queue_has_capacity(send_kind kind, size_t payload_size) const noexcept;
	void remember_write_error(uint64_t sequence, error_code error) noexcept;

	[[nodiscard]] error_code observe_write_error(uint64_t target) noexcept;
	void complete_write_waiters();

	void deliver_write_waiter(const std::shared_ptr<write_waiter> &waiter,
		error_code error, bool clear_slot = true
	) noexcept;

	void cancel_write_waiter(uint64_t id) noexcept;

	void deliver_send_completion(const std::shared_ptr<send_operation> &operation,
		error_code error
	) noexcept;

	void complete_send_operation(const std::shared_ptr<send_operation> &operation,
		error_code error
	) noexcept;

	void commit_data_frame(send_operation &operation) noexcept;

	void install_send_cancellation(const std::shared_ptr<send_operation> &operation);
	void cancel_queued_send(uint64_t id) noexcept;

	void start_wire_frame(prepared_frame frame, wire_frame_kind kind,
		std::shared_ptr<send_operation> operation = {}
	) noexcept;

	[[nodiscard]] error_code enqueue_send_operation(
		std::shared_ptr<send_operation> operation
	) noexcept;

	[[nodiscard]] static sys_expected<> retain_protocol_payload (
		optional<std::vector<std::byte>> &slot, const std::vector<std::byte> &payload
	) noexcept;

private:
	send_engine_owner &m_owner;
	frame_builder m_frame_builder {};

	size_t m_max_queued_write_bytes = 0;
	size_t m_max_queued_write_operations = 0;
	size_t m_max_message_size = 0;

	optional<message_type> m_outgoing_message_type {};
	size_t m_outgoing_message_size = 0;
	optional<utf8_validator> m_outgoing_utf8 {};

	optional<std::vector<std::byte>> m_pending_auto_pong {};
	optional<prepared_frame> m_pending_local_close {};

	optional<std::vector<std::byte>> m_pending_close_response {};
	optional<prepared_frame> m_pending_protocol_close {};

	bool m_wire_write_active = false;
	bool m_last_wire_was_control = false;

	std::shared_ptr<send_operation> m_current_data {};
	std::deque<std::shared_ptr<send_operation>> m_data_write_queue {};
	std::deque<std::shared_ptr<send_operation>> m_control_write_queue {};

	size_t m_queued_write_operations = 0;
	size_t m_queued_write_bytes = 0;

	uint64_t m_last_write_sequence = 0;
	uint64_t m_completed_write_sequence = 0;
	uint64_t m_next_send_operation_id = 0;

	optional<std::pair<uint64_t,error_code>> m_unobserved_write_error {};
	std::deque<std::shared_ptr<write_waiter>> m_write_waiters {};

	uint64_t m_next_write_waiter_id = 0;
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_SEND_ENGINE_H
