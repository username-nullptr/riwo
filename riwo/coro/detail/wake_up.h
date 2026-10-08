// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_WAKE_UP_H
#define RIWO_CORO_DETAIL_WAKE_UP_H

#include <riwo/coro/global.h>

namespace riwo::coro::detail
{

class RIWO_CORO_API lock_wake_up final :
	public std::enable_shared_from_this<lock_wake_up>
{
	RIWO_DISABLE_COPY_MOVE(lock_wake_up)

public:
	using ptr_t = std::shared_ptr<lock_wake_up>;
	using handler_t = async_work<bool>::handler_t;

	lock_wake_up(asio::any_io_executor exec, handler_t handler);
	~lock_wake_up();

	bool operator()(bool success);
	[[nodiscard]] asio::any_io_executor get_executor() noexcept;

	void start_timer(const auto &timeout)
	{
		timer().emplace(get_executor());
		if constexpr( requires { timeout.time_since_epoch(); } )
		{
			using timeout_clock_t = std::remove_cvref_t<decltype(timeout)>::clock;
			auto now = timeout_clock_t::now();

			timer()->expires_after(timeout <= now ?
				asio::steady_timer::duration::zero() :
				std::chrono::duration_cast<asio::steady_timer::duration>(timeout - now));
		}
		else
		{
			timer()->expires_after (
				std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
			);
		}
		timer()->async_wait (
		[self = shared_from_this()](const error_code &error) mutable
		{
			if( not error )
				(*self)(false);
		});
	}

private:
	[[nodiscard]] optional<asio::steady_timer> &timer() noexcept;
	class impl;
	impl *m_impl;
};

using lock_wake_up_ptr = lock_wake_up::ptr_t;

} //namespace riwo::coro::detail


#endif //RIWO_CORO_DETAIL_WAKE_UP_H
