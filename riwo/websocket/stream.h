// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_STREAM_H
#define RIWO_WEBSOCKET_STREAM_H

#include <riwo/websocket/types.h>
#include <riwo/http/utils/connection.h>

namespace riwo::websocket
{
/**
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * One read-side operation and one write-side operation may overlap. Message
 * writes are a high-level extension and are serialized by the bounded queue.
 * With multiple executor runners, use one strand for all calls and handlers
 * that access this stream.
 */
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_WEBSOCKET_TAPI basic_stream
{
	RIWO_DISABLE_COPY(basic_stream)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using config_t = stream_config;

	using close_frame_t = close_frame;
	using close_info_t = close_info;

	using ctrl_payload_t = ctrl_payload;
	using closed_callback_t = closed_callback;

	using message_chunk_t = message_chunk;
	using message_info_t = message_info;
	using adopt_options_t = adopt_options;

	using connection_t = http::basic_connection<executor_t>;
	using connection_ptr = std::shared_ptr<connection_t>;

	template <typename Buffer>
	using recv_buf = basic_message<Buffer>;
	using body_type = message_type;

public:
	basic_stream() requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	explicit basic_stream(config_t config) requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_stream(Exec0 &&exec, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_stream> and
		core_concepts::match_sched<Exec0,executor_t>
	);
	basic_stream(basic_stream &&other) noexcept;
	basic_stream &operator=(basic_stream &&other) noexcept;
	~basic_stream();

public:
	template <typename Token, typename...Args>
	static constexpr bool task_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,Args...>;

	template <typename Token, typename...Args>
	static constexpr bool completion_token_v =
		core_concepts::tf_opt_token<Token,error_code,Args...>;

	template <typename Func>
	static constexpr bool control_callback_v =
		std::invocable<std::remove_reference_t<Func>&,ctrl_payload_t&>;

public:
	basic_stream &adopt (
		connection_ptr connection, adopt_options_t options
	);
	basic_stream &adopt (
		connection_ptr connection, adopt_options_t options,
		error_code &error
	) noexcept;

public:
	template <typename Buffer = std::vector<std::byte>, typename Token = use_sync_t>
	[[nodiscard]] auto read(Token &&token = {}) requires
		concepts::buffer<Buffer> and task_token_v<Token,basic_message<Buffer>>;

	template <typename Buffer = std::vector<std::byte>, typename Token = use_sync_t>
	[[nodiscard]] auto read_frame(Token &&token = {}) requires
		concepts::buffer<Buffer> and task_token_v<Token,basic_data_frame<Buffer>>;

	template <typename Consumer, typename Token = use_sync_t>
	[[nodiscard]] auto consume(Consumer &&consumer, Token &&token = {}) requires (
		std::invocable<std::remove_reference_t<Consumer>&,const message_chunk_t&> and
		std::same_as<std::invoke_result_t<std::remove_reference_t<Consumer>&,const message_chunk_t&>,void> and
		task_token_v<Token,message_info_t>
	);

public:
	template <message_type Type, typename Token = use_sync_t>
	auto write(const const_buffer &body, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write(message_type type, const const_buffer &body, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write(message_type type, std::span<const const_buffer> body, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write_text(std::string_view text, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write_binary(const const_buffer &body, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Buffer, typename Token = use_sync_t>
	auto write_frame(const basic_data_frame<Buffer> &frame, Token &&token = {})
		requires concepts::buffer<Buffer> and completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto wait_written(Token &&token = {})
		requires task_token_v<Token>;

public:
	template <message_type Type, typename Token = use_sync_t>
	auto write(const const_buffer &body, write_options options, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write(message_type type, const const_buffer &body, write_options options, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write(message_type type, std::span<const const_buffer> body, write_options options, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write_text(std::string_view text, write_options options, Token &&token = {})
		requires completion_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write_binary(const const_buffer &body, write_options options, Token &&token = {})
		requires completion_token_v<Token,size_t>;

public:
	template <typename Func>
	basic_stream &on_ping(Func &&callback)
		requires control_callback_v<Func>;

	template <typename Func>
	basic_stream &on_pong(Func &&callback)
		requires control_callback_v<Func>;

	template <typename Token = use_sync_t>
	auto ping(Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto ping(const const_buffer &payload, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto pong(Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto pong(const const_buffer &payload, Token &&token = {})
		requires task_token_v<Token,size_t>;

public:
	basic_stream &on_closed(closed_callback_t callback);

	template <typename Token = use_sync_t>
	[[nodiscard]] auto wait_closed(Token &&token = {})
		requires task_token_v<Token,close_info_t>;

	template <typename Token = use_sync_t>
	auto close(Token &&token = {})
		requires completion_token_v<Token,close_info_t>;

	template <typename Token = use_sync_t>
	auto close(close_frame_t frame, Token &&token = {})
		requires completion_token_v<Token,close_info_t>;

public:
	basic_stream &cancel(error_code &error) noexcept;
	basic_stream &cancel();

	basic_stream &shutdown(error_code &error) noexcept;
	basic_stream &shutdown();

public:
	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] connection_state state() const noexcept;

	[[nodiscard]] bool is_open() const noexcept;
	[[nodiscard]] bool is_closing() const noexcept;

	[[nodiscard]] role stream_role() const noexcept;
	[[nodiscard]] std::string negotiated_subprotocol() const;
	[[nodiscard]] std::vector<extension> negotiated_extensions() const;

	[[nodiscard]] config_t config() const noexcept;
	[[nodiscard]] optional<close_info_t> peer_close() const;

	[[nodiscard]] http::endpoint remote_endpoint() const noexcept;
	[[nodiscard]] http::endpoint local_endpoint() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using stream = basic_stream<>;

} //namespace riwo::websocket
#include <riwo/websocket/detail/stream.h>


#endif //RIWO_WEBSOCKET_STREAM_H
