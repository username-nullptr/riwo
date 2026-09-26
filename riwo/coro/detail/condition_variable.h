// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_CONDITION_VARIABLE_H
#define RIWO_CORO_DETAIL_CONDITION_VARIABLE_H

namespace riwo::coro { namespace detail
{

RIWO_CORO_API awaitable<bool> condition_variable_wait (
	condition_variable_impl *impl, asio::any_io_executor exec,
	std::function<void()> unlock
);
RIWO_CORO_API awaitable<bool> condition_variable_wait (
	condition_variable_impl *impl, asio::any_io_executor exec,
	std::function<void()> unlock, asio::steady_timer::duration timeout
);
RIWO_CORO_API awaitable<bool> condition_variable_wait (
	condition_variable_impl *impl, asio::any_io_executor exec,
	std::function<void()> unlock, asio::steady_timer::time_point timeout
);

} //namespace detail

template <typename Mutex>
awaitable<void> condition_variable::wait(unique_lock<Mutex> &lock) noexcept
{
	co_return co_await wait(co_await asio::this_coro::executor, lock);
}

template <typename Mutex>
awaitable<void> condition_variable::wait(unique_lock<Mutex> &lock, auto pred)
{
	co_return co_await wait(
		co_await asio::this_coro::executor, lock, std::move(pred)
	);
}

template <typename Mutex>
awaitable<void> condition_variable::wait
(concepts::sched auto &&exec, unique_lock<Mutex> &lock) noexcept
{
	auto _exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	co_await detail::condition_variable_wait(
		m_impl, _exec, [&lock] { lock.unlock(); }
	);
	co_await lock.lock(_exec);
	co_return ;
}

template <typename Mutex>
awaitable<void> condition_variable::wait
(concepts::sched auto &&exec, unique_lock<Mutex> &lock, auto pred)
{
	auto _exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	while( not pred() )
		co_await wait(_exec, lock);
	co_return ;
}

template <typename Mutex, typename Rep, typename Period>
awaitable<bool> condition_variable::wait_for
(unique_lock<Mutex> &lock, const duration<Rep,Period> &rtime)
{
	co_return co_await wait_for (
		co_await asio::this_coro::executor, lock, rtime
	);
}

template <typename Mutex, typename Rep, typename Period>
awaitable<bool> condition_variable::wait_for
(unique_lock<Mutex> &lock, const duration<Rep,Period> &rtime, auto pred)
{
	co_return co_await wait_for (
		co_await asio::this_coro::executor, lock, rtime, std::move(pred)
	);
}

template <typename Mutex, typename Clock, typename Duration>
awaitable<bool> condition_variable::wait_until
(unique_lock<Mutex> &lock, const time_point<Clock,Duration> &atime)
{
	co_return co_await wait_until (
		co_await asio::this_coro::executor, lock, atime
	);
}

template <typename Mutex, typename Clock, typename Duration>
awaitable<bool> condition_variable::wait_until
(unique_lock<Mutex> &lock, const time_point<Clock,Duration> &atime, auto pred)
{
	co_return co_await wait_until (
		co_await asio::this_coro::executor, lock, atime, std::move(pred)
	);
}

template <typename Mutex, typename Rep, typename Period>
awaitable<bool> condition_variable::wait_for
(concepts::sched auto &&exec, unique_lock<Mutex> &lock, const duration<Rep,Period> &rtime)
{
	auto _exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	auto notified = co_await detail::condition_variable_wait (
		m_impl, _exec, [&lock] { lock.unlock(); },
		std::chrono::duration_cast<asio::steady_timer::duration>(rtime)
	);
	co_await lock.lock(_exec);
	co_return notified;
}

template <typename Mutex, typename Rep, typename Period>
awaitable<bool> condition_variable::wait_for
(concepts::sched auto &&exec, unique_lock<Mutex> &lock, const duration<Rep,Period> &rtime, auto pred)
{
	auto atime = std::chrono::steady_clock::now() +
		std::chrono::duration_cast<std::chrono::steady_clock::duration>(rtime);

	co_return co_await wait_until (
		std::forward<decltype(exec)>(exec), lock, atime, std::move(pred)
	);
}

template <typename Mutex, typename Clock, typename Duration>
awaitable<bool> condition_variable::wait_until
(concepts::sched auto &&exec, unique_lock<Mutex> &lock, const time_point<Clock,Duration> &atime)
{
	auto _exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	const auto now = Clock::now();

	const auto wait_time = atime <= now ?
		asio::steady_timer::duration::zero() :
		std::chrono::duration_cast<asio::steady_timer::duration>(atime - now);

	const auto deadline = asio::steady_timer::clock_type::now() + wait_time;
	auto notified = co_await detail::condition_variable_wait (
		m_impl, _exec, [&lock] { lock.unlock(); }, deadline
	);
	co_await lock.lock(_exec);
	co_return notified;
}

template <typename Mutex, typename Clock, typename Duration>
awaitable<bool> condition_variable::wait_until
(concepts::sched auto &&exec, unique_lock<Mutex> &lock, const time_point<Clock,Duration> &atime, auto pred)
{
	auto _exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	while( not pred() )
	{
		if( not co_await wait_until(_exec, lock, atime) )
			co_return pred();
	}
	co_return true;
}

} //namespace riwo::coro


#endif //RIWO_CORO_DETAIL_CONDITION_VARIABLE_H
