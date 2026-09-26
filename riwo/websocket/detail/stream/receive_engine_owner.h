// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_OWNER_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_OWNER_H

#include <riwo/websocket/detail/stream/receive_operations.h>

namespace riwo::websocket::detail
{

class receive_engine;

class RIWO_WEBSOCKET_API receive_engine_owner
{
public:
	virtual ~receive_engine_owner() = default;

	[[nodiscard]] virtual std::shared_ptr<receive_engine_owner> receive_owner() noexcept = 0;
	[[nodiscard]] virtual std::weak_ptr<receive_engine_owner> weak_receive_owner() noexcept = 0;

	[[nodiscard]] virtual receive_engine &receive_side() noexcept = 0;
	[[nodiscard]] virtual asio::any_io_executor receive_executor() const noexcept = 0;

	[[nodiscard]] virtual size_t read_transport (
		const mutable_buffer &buffer, error_code &error
	) noexcept = 0;

	[[nodiscard]] virtual awaitable<std::tuple<error_code,size_t>>
	async_read_transport(std::shared_ptr<std::vector<std::byte>> storage) = 0;

	[[nodiscard]] virtual error_code read_state_error() const noexcept = 0;
	[[nodiscard]] virtual error_code frame_read_state_error() const noexcept = 0;
	[[nodiscard]] virtual error_code consume_state_error() const noexcept = 0;
	[[nodiscard]] virtual error_code finish_receive_eof() noexcept = 0;

	virtual void handle_receive_failure(error_code error) noexcept = 0;
	virtual void handle_receive_protocol_failure(error_code error, bool synchronous) noexcept = 0;

	[[nodiscard]] virtual sys_expected<> handle_sync_control (
		opcode op, std::vector<std::byte> &payload
	) noexcept = 0;

	[[nodiscard]] virtual awaitable<error_code> handle_async_control (
		opcode op, std::vector<std::byte> &payload
	) = 0;

	[[nodiscard]] virtual sys_expected<> handle_sync_peer_close (
		const std::vector<std::byte> &payload
	) noexcept = 0;

	[[nodiscard]] virtual sys_expected<> begin_peer_close (
		const std::vector<std::byte> &payload
	) noexcept = 0;

	[[nodiscard]] virtual bool close_receive_pending() const noexcept = 0;
	virtual void start_close_receive() noexcept = 0;

	[[nodiscard]] virtual sys_expected<> remember_close_receive_peer (
		const std::vector<std::byte> &payload
	) noexcept = 0;

	virtual void complete_close_receive (
		const std::exception_ptr &exception, error_code error
	) noexcept = 0;
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_OWNER_H
