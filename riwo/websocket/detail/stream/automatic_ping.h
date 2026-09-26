// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_AUTOMATIC_PING_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_AUTOMATIC_PING_H

#include <riwo/websocket/global.h>

namespace riwo::websocket::detail
{

class RIWO_WEBSOCKET_API automatic_ping final :
	public std::enable_shared_from_this<automatic_ping>
{
	RIWO_DISABLE_COPY_MOVE(automatic_ping)
	using payload_t = std::array<std::byte,8>;

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using write_fn_t = void (*)(void*, const const_buffer&, io_handler_t);
	using fail_fn_t = void (*)(void*, error_code) noexcept;

	automatic_ping(const asio::any_io_executor &exec, std::weak_ptr<void> owner,
		std::chrono::milliseconds interval, size_t timeout_retries,
		write_fn_t write_fn, fail_fn_t fail_fn
	);
	~automatic_ping();

public:
	[[nodiscard]] static sys_expected<std::shared_ptr<automatic_ping>> create (
		const asio::any_io_executor &exec, std::weak_ptr<void> owner, std::chrono::milliseconds interval,
		size_t timeout_retries, write_fn_t write_fn, fail_fn_t fail_fn
	) noexcept;

	[[nodiscard]] sys_expected<> start() noexcept;
	void acknowledge(std::span<const std::byte> payload) noexcept;
	void stop() noexcept;

private:
	[[nodiscard]] sys_expected<> schedule() noexcept;
	void timer_completed(error_code error) noexcept;
	void write_completed(error_code error) noexcept;
	void report_failure(error_code error) noexcept;

private:
	asio::steady_timer m_timer;
	std::weak_ptr<void> m_owner;
	std::chrono::milliseconds m_interval;

	size_t m_timeout_retries;
	write_fn_t m_write_fn;
	fail_fn_t m_fail_fn;

	optional<payload_t> m_awaited_pong {};
	uint64_t m_next_ping_id = 0;

	size_t m_consecutive_timeouts = 0;
	bool m_active = false;
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_AUTOMATIC_PING_H
