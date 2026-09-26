// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_SHARED_MUTEX_H
#define RIWO_CORO_DETAIL_SHARED_MUTEX_H

namespace riwo::coro { namespace detail
{

RIWO_CORO_API awaitable<void> shared_mutex_lock (
	shared_mutex_impl *impl, asio::any_io_executor exec
);
RIWO_CORO_API awaitable<void> shared_mutex_lock_shared (
	shared_mutex_impl *impl, asio::any_io_executor exec
);
RIWO_CORO_API awaitable<bool> shared_mutex_try_lock_for (
	shared_mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::duration timeout
);
RIWO_CORO_API awaitable<bool> shared_mutex_try_lock_until (
	shared_mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::time_point timeout
);
RIWO_CORO_API awaitable<bool> shared_mutex_try_lock_shared_for (
	shared_mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::duration timeout
);
RIWO_CORO_API awaitable<bool> shared_mutex_try_lock_shared_until (
	shared_mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::time_point timeout
);

RIWO_CORO_API awaitable<void> shared_lock_lock_shared (
	shared_lock_impl *impl, asio::any_io_executor exec
);
RIWO_CORO_API awaitable<bool> shared_lock_try_lock_shared_for (
	shared_lock_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::duration timeout
);
RIWO_CORO_API awaitable<bool> shared_lock_try_lock_shared_until (
	shared_lock_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::time_point timeout
);

} //namespace detail

awaitable<void> shared_mutex::lock(concepts::sched auto &&exec)
{
	return detail::shared_mutex_lock(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec))
	);
}

awaitable<void> shared_mutex::lock_shared(concepts::sched auto &&exec)
{
	return detail::shared_mutex_lock_shared(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec))
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_mutex::try_lock_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	return detail::shared_mutex_try_lock_for(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)),
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_mutex::try_lock_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	const auto now = Clock::now();
	const auto wait_time = timeout <= now ? asio::steady_timer::duration::zero() :
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout - now);
	const auto deadline = asio::steady_timer::clock_type::now() + wait_time;
	return detail::shared_mutex_try_lock_until(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)), deadline
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_mutex::try_lock_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_lock_for(
		co_await asio::this_coro::executor, timeout
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_mutex::try_lock_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_lock_until(
		co_await asio::this_coro::executor, timeout
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_mutex::try_lock_shared_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	return detail::shared_mutex_try_lock_shared_for(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)),
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_mutex::try_lock_shared_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	const auto now = Clock::now();
	const auto wait_time = timeout <= now ?
		asio::steady_timer::duration::zero() :
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout - now);

	const auto deadline = asio::steady_timer::clock_type::now() + wait_time;
	return detail::shared_mutex_try_lock_shared_until(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)), deadline
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_mutex::try_lock_shared_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_lock_shared_for (
		co_await asio::this_coro::executor, timeout
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_mutex::try_lock_shared_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_lock_shared_until (
		co_await asio::this_coro::executor, timeout
	);
}

awaitable<void> shared_lock::lock_shared(concepts::sched auto &&exec)
{
	return detail::shared_lock_lock_shared(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec))
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_lock::try_lock_shared_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	return detail::shared_lock_try_lock_shared_for(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)),
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_lock::try_lock_shared_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	const auto now = Clock::now();
	const auto wait_time = timeout <= now ?
		asio::steady_timer::duration::zero() :
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout - now);

	const auto deadline = asio::steady_timer::clock_type::now() + wait_time;
	return detail::shared_lock_try_lock_shared_until(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)), deadline
	);
}

template<typename Rep, typename Period>
awaitable<bool> shared_lock::try_lock_shared_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_lock_shared_for (
		co_await asio::this_coro::executor, timeout
	);
}

template<typename Clock, typename Duration>
awaitable<bool> shared_lock::try_lock_shared_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_lock_shared_until (
		co_await asio::this_coro::executor, timeout
	);
}

} //namespace riwo::coro


#endif //RIWO_CORO_DETAIL_SHARED_MUTEX_H
