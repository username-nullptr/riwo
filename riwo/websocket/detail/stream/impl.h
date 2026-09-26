// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H

#ifndef RIWO_WEBSOCKET_STREAM_H
# error "Include <riwo/websocket/stream.h> instead."
#endif

#include <riwo/websocket/detail/stream/close_operations.h>
#include <riwo/websocket/detail/stream/close_wait_queue.h>
#include <riwo/websocket/detail/stream/close_deadline.h>

#include <riwo/websocket/detail/stream/stream_transport.h>
#include <riwo/websocket/detail/stream/automatic_ping.h>

#include <riwo/websocket/detail/stream/receive_engine.h>
#include <riwo/websocket/detail/stream/send_engine.h>

namespace riwo::websocket
{

template <core_concepts::exec Exec>
class RIWO_WEBSOCKET_TAPI basic_stream<Exec>::impl : public std::enable_shared_from_this<impl>,
	public detail::send_engine_owner, public detail::receive_engine_owner
{
	RIWO_DISABLE_COPY_MOVE(impl)
	friend class basic_stream;

	enum class local_close_phase : uint8_t {
		none, queued, sent,
	};

public:
	using adopt_options_t = adopt_options;
	using close_info_t = close_info;

	using ctrl_payload_t = ctrl_payload;
	using sync_control_callback_t = std::function<void(ctrl_payload_t&)>;

	using async_control_callback_t = std::function<awaitable<void>(ctrl_payload_t&)>;
	using closed_callback_t = closed_callback;

	using prepared_frame = detail::prepared_frame;
	using close_wait_operation = detail::close_wait_operation;

	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using void_handler_t = asio::any_completion_handler<void(error_code)>;

	using message_handler_t = asio::any_completion_handler<void(error_code,message)>;
	using frame_handler_t = asio::any_completion_handler<void(error_code,data_frame)>;

	using info_handler_t = asio::any_completion_handler<void(error_code,message_info_t)>;
	using close_handler_t = asio::any_completion_handler<void(error_code,close_info_t)>;

	explicit impl(executor_t exec, const config_t &config);

public:
	void adopt(connection_ptr connection,
		adopt_options_t options, error_code &error
	) noexcept;

	[[nodiscard]] size_t write(message_type type,
		std::span<const const_buffer> buffers, write_options options, error_code &error
	) noexcept;

	[[nodiscard]] size_t write_frame(message_type type,
		const const_buffer &payload, bool continuation, bool fin, error_code &error
	) noexcept;

	void async_write_message(message_type type, std::span<const const_buffer> buffers,
		write_options options, std::shared_ptr<std::vector<std::byte>> payload_owner,
		io_handler_t handler
	);

	void async_write_frame(message_type type, const const_buffer &payload,
		bool continuation, bool fin, std::shared_ptr<std::vector<std::byte>> payload_owner,
		io_handler_t handler
	);
	[[nodiscard]] size_t write_control (
		opcode op, const const_buffer &payload, error_code &error, bool automatic = false
	) noexcept;

	void async_write_control (
		opcode op, const const_buffer &payload, io_handler_t handler, bool automatic = false
	);

	void wait_written(error_code &error) noexcept;
	void async_wait_written(void_handler_t handler);

	[[nodiscard]] message read(error_code &error) noexcept;
	[[nodiscard]] data_frame read_frame(error_code &error) noexcept;

	void async_read_message(message_handler_t handler);
	void async_read_frame(frame_handler_t handler);

	template <typename Consumer>
	[[nodiscard]] message_info consume(Consumer &&consumer, error_code &error) noexcept;

	template <typename Consumer>
	void async_consume(Consumer &&consumer, info_handler_t handler);

	template <typename Buffer>
	[[nodiscard]] static basic_message<Buffer> convert_message (
		message value, error_code &error
	) noexcept;

	template <typename Buffer>
	[[nodiscard]] static basic_data_frame<Buffer> convert_frame (
		data_frame value, error_code &error
	) noexcept;

	void on_ping(sync_control_callback_t callback);
	void on_ping(async_control_callback_t callback);
	void on_pong(sync_control_callback_t callback);
	void on_pong(async_control_callback_t callback);
	void on_closed(closed_callback_t callback);

	[[nodiscard]] close_info_t close(const close_frame &frame, error_code &error) noexcept;
	void async_close(const close_frame &frame, close_handler_t completion);

	[[nodiscard]] close_info_t wait_closed(error_code &error) noexcept;
	void async_wait_closed(close_handler_t completion);

	void cancel(error_code &error) noexcept;
	void shutdown(error_code &error) noexcept;

public:
	[[nodiscard]] detail::receive_engine &receive_side() noexcept override;
	[[nodiscard]] std::shared_ptr<receive_engine_owner> receive_owner() noexcept override;

	[[nodiscard]] std::weak_ptr<receive_engine_owner> weak_receive_owner() noexcept override;
	[[nodiscard]] asio::any_io_executor receive_executor() const noexcept override;

	[[nodiscard]] detail::send_engine &send_side() noexcept override;
	[[nodiscard]] std::shared_ptr<send_engine_owner> send_owner() noexcept override;

	[[nodiscard]] std::weak_ptr<send_engine_owner> weak_send_owner() noexcept override;
	[[nodiscard]] asio::any_io_executor send_executor() const noexcept override;

	[[nodiscard]] size_t write_prepared (
		const prepared_frame &frame, error_code &error
	) noexcept override;

	[[nodiscard]] size_t read_transport (
		const mutable_buffer &buffer, error_code &error
	) noexcept override;

	[[nodiscard]] awaitable<std::tuple<error_code,size_t>>
	async_read_transport(std::shared_ptr<std::vector<std::byte>> storage) override;

	[[nodiscard]] executor_t executor() const noexcept;

	[[nodiscard]] error_code write_state_error() const noexcept override;
	[[nodiscard]] error_code read_state_error() const noexcept override;

	[[nodiscard]] error_code frame_read_state_error() const noexcept override;
	[[nodiscard]] error_code frame_write_state_error() const noexcept override;

	[[nodiscard]] error_code consume_state_error() const noexcept override;
	[[nodiscard]] bool send_transport_ready() const noexcept override;
	[[nodiscard]] bool automatic_control_enabled() const noexcept;

	[[nodiscard]] bool close_receive_pending() const noexcept override;
	[[nodiscard]] bool protocol_failure_active() const noexcept override;

	[[nodiscard]] error_code protocol_failure_error(error_code fallback) const noexcept override;
	[[nodiscard]] error_code finish_receive_eof() noexcept override;

	[[nodiscard]] sys_expected<> handle_sync_ping (
		ctrl_payload_t &payload
	) noexcept;

	[[nodiscard]] sys_expected<> handle_sync_control (
		opcode op, std::vector<std::byte> &payload
	) noexcept override;

	[[nodiscard]] awaitable<error_code> handle_async_control (
		opcode op, std::vector<std::byte> &payload
	) override;

	[[nodiscard]] sys_expected<> handle_sync_peer_close (
		const std::vector<std::byte> &payload
	) noexcept override;

	void start_transport_write(prepared_frame frame,
		detail::wire_frame_kind kind, std::shared_ptr<detail::send_operation> operation
	) noexcept override;

	static void start_async_transport_read(void *connection,
		const mutable_buffer &buffer, io_handler_t handler
	);
	static void start_async_transport_write(void *connection,
		std::span<const const_buffer> buffers, io_handler_t handler
	);
	void handle_send_failure(error_code error) noexcept override;
	void handle_wire_frame_sent(detail::wire_frame_kind kind) noexcept override;

	void handle_receive_failure(error_code error) noexcept override;
	void handle_receive_protocol_failure(error_code error, bool synchronous) noexcept override;

	[[nodiscard]] sys_expected<> queue_auto_pong (
		const std::vector<std::byte> &payload
	) noexcept;

	[[nodiscard]] sys_expected<> begin_peer_close (
		const std::vector<std::byte> &payload
	) noexcept override;

	void start_close_receive() noexcept override;
	[[nodiscard]] sys_expected<> start_automatic_ping() noexcept;

	void acknowledge_automatic_pong(const std::vector<std::byte> &payload) noexcept;
	void stop_automatic_ping() noexcept;

	static void write_automatic_ping(void *owner, const const_buffer &payload, io_handler_t handler);
	static void fail_automatic_ping(void *owner, error_code error) noexcept;

	[[nodiscard]] sys_expected<> remember_close_receive_peer (
		const std::vector<std::byte> &payload
	) noexcept override;

	void complete_close_receive (
		const std::exception_ptr &exception, error_code error
	) noexcept override;

private:
	void close_transport(error_code &error, bool cancel_first = false) noexcept;
	[[nodiscard]] bool local_close_started() const noexcept;
	[[nodiscard]] bool local_close_sent() const noexcept;

	void mark_local_close_queued() noexcept;
	void mark_local_close_sent() noexcept;

	[[nodiscard]] sys_expected<> remember_peer_close (
		const std::vector<std::byte> &payload
	) noexcept;

	[[nodiscard]] close_info_t retained_close_info(bool clean = false) const;

	void begin_local_close(prepared_frame frame) noexcept;
	void start_close_deadline() noexcept;
	static void close_deadline_expired(void *owner) noexcept;

	void finish_close(error_code error, bool clean,
		bool cancel_transport = false
	) noexcept;

	bool add_close_waiter(close_handler_t completion) noexcept;
	void complete_close_waiters(error_code error) noexcept;
	void cancel_close_waiter(uint64_t id) noexcept;

	static void cancel_close_waiter(void *owner, uint64_t id) noexcept;
	void notify_closed() noexcept;

	// Protocol and transport failure handling.
	[[nodiscard]] static optional<close_code>
	protocol_failure_code(error_code error) noexcept;

	void begin_protocol_failure(error_code error, bool synchronous = false) noexcept;
	void finish_protocol_failure(bool cancel_transport = false) noexcept;
	void fail(error_code error) noexcept;

private:
	executor_t m_exec {};
	config_t m_config {};

	connection_ptr m_connection;
	connection_state m_state = connection_state::idle;

	role m_role = role::client;
	std::string m_subprotocol {};
	std::vector<extension> m_extensions {};

	detail::receive_engine m_receive_engine;
	detail::send_engine m_send_engine;
	detail::stream_transport m_stream_transport {};

	sync_control_callback_t m_on_ping {};
	async_control_callback_t m_on_async_ping {};
	sync_control_callback_t m_on_pong {};
	async_control_callback_t m_on_async_pong {};
	std::shared_ptr<detail::automatic_ping> m_automatic_ping {};

	optional<close_info_t> m_peer_close {};
	optional<close_info_t> m_close_result {};

	detail::close_deadline m_close_deadline {};
	detail::close_wait_queue m_close_wait_queue {};
	local_close_phase m_local_close_phase = local_close_phase::none;

	// Terminal failure and transport state.
	bool m_protocol_failure_active = false;
	error_code m_error {};

	bool m_transport_closed = false;
};

} //namespace riwo::websocket
#include <riwo/websocket/detail/permessage_deflate.h>

#include <riwo/websocket/detail/stream/impl/core.ipp>
#include <riwo/websocket/detail/stream/impl/transport.ipp>

#include <riwo/websocket/detail/stream/impl/write.ipp>
#include <riwo/websocket/detail/stream/impl/read.ipp>

#include <riwo/websocket/detail/stream/impl/control.ipp>
#include <riwo/websocket/detail/stream/impl/close.ipp>

#include <riwo/websocket/detail/stream/impl/lifecycle.ipp>


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
