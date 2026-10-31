// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_WEBSOCKET_DETAIL_STREAM_CLOSE_WAIT_QUEUE_H
#define RIWO_EXP_WEBSOCKET_DETAIL_STREAM_CLOSE_WAIT_QUEUE_H

#include <riwo/experimental/websocket/detail/stream/close_operations.h>

namespace riwo::experimental::websocket::detail
{

class RIWO_EXP_WEBSOCKET_API close_wait_queue final
{
	RIWO_DISABLE_COPY_MOVE(close_wait_queue)

public:
	using handler_t = asio::any_completion_handler<void(error_code,close_info)>;
	using cancel_fn_t = void (*)(void*, uint64_t) noexcept;

	close_wait_queue() noexcept;
	~close_wait_queue();

public:
	void set_callback(closed_callback callback);
	void notify(const close_info &result) noexcept;

	[[nodiscard]] bool add(handler_t completion, asio::any_io_executor exec,
		std::weak_ptr<void> owner, cancel_fn_t cancel_fn
	) noexcept;

	static void post(handler_t completion, const asio::any_io_executor &exec,
		error_code error, close_info result
	);
	void cancel(uint64_t id) noexcept;
	void complete(error_code error, const close_info &result) noexcept;

	[[nodiscard]] bool notified() const noexcept;

private:
	closed_callback m_callback {};
	std::deque<close_wait_operation_ptr> m_waiters {};

	uint64_t m_next_id = 0;
	bool m_notified = false;
};

} //namespace riwo::experimental::websocket::detail


#endif //RIWO_EXP_WEBSOCKET_DETAIL_STREAM_CLOSE_WAIT_QUEUE_H
