// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_MUTEX_H
#define RIWO_CORO_DETAIL_MUTEX_H

namespace riwo::coro { namespace detail
{

RIWO_CORO_API awaitable<void> mutex_lock (
	mutex_impl *impl, asio::any_io_executor exec
);
RIWO_CORO_API awaitable<bool> mutex_try_lock_for (
	mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::duration timeout
);
RIWO_CORO_API awaitable<bool> mutex_try_lock_until (
	mutex_impl *impl, asio::any_io_executor exec,
	asio::steady_timer::time_point timeout
);

} //namespace detail

awaitable<void> mutex::lock(concepts::sched auto &&exec)
{
	return detail::mutex_lock(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec))
	);
}

template<typename Rep, typename Period>
awaitable<bool> mutex::try_lock_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	return detail::mutex_try_lock_for(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)),
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
	);
}

template<typename Clock, typename Duration>
awaitable<bool> mutex::try_lock_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	const auto now = Clock::now();
	const auto wait_time = timeout <= now ?
		asio::steady_timer::duration::zero() :
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout - now);

	const auto deadline = asio::steady_timer::clock_type::now() + wait_time;
	return detail::mutex_try_lock_until(m_impl,
		get_executor_helper(std::forward<decltype(exec)>(exec)), deadline
	);
}

template<typename Rep, typename Period>
awaitable<bool> mutex::try_lock_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_lock_for(
		co_await asio::this_coro::executor, timeout
	);
}

template<typename Clock, typename Duration>
awaitable<bool> mutex::try_lock_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_lock_until(
		co_await asio::this_coro::executor, timeout
	);
}

template <typename Mutex>
unique_lock<Mutex>::unique_lock(mutex_t &mutex) :
	m_mutex(&mutex)
{

}

template <typename Mutex>
unique_lock<Mutex>::~unique_lock() noexcept(noexcept(m_mutex->unlock()))
{
	if( m_mutex and m_owns )
	{
		m_owns = false;
		m_mutex->unlock();
	}
}

template <typename Mutex>
unique_lock<Mutex>::unique_lock(unique_lock &&other) noexcept :
	m_mutex(other.m_mutex), m_owns(other.m_owns)
{
	other.m_mutex = nullptr;
	other.m_owns = false;
}

template <typename Mutex>
unique_lock<Mutex> &unique_lock<Mutex>::operator=(unique_lock &&other) noexcept
{
	if( this == &other )
		return *this;

	if( m_mutex and m_owns )
	{
		m_owns = false;
		m_mutex->unlock();
	}
	m_mutex = other.m_mutex;
	m_owns = other.m_owns;

	other.m_mutex = nullptr;
	other.m_owns = false;
	return *this;
}

template <typename Mutex>
awaitable<void> unique_lock<Mutex>::lock(concepts::sched auto &&exec)
{
	if( m_mutex and not m_owns )
	{
		co_await m_mutex->lock(std::forward<decltype(exec)>(exec));
		m_owns = true;
	}
	co_return ;
}

template <typename Mutex>
awaitable<void> unique_lock<Mutex>::lock()
{
	co_return co_await lock(co_await asio::this_coro::executor);
}

template <typename Mutex>
bool unique_lock<Mutex>::try_lock()
{
	if( not m_mutex )
		return false;

	else if( m_owns )
		return true;

	return m_owns = m_mutex->try_lock();
}

template <typename Mutex>
void unique_lock<Mutex>::unlock()
{
	if( m_mutex and m_owns )
	{
		m_owns = false;
		m_mutex->unlock();
	}
}

template <typename Mutex>
template<typename Rep, typename Period>
awaitable<bool> unique_lock<Mutex>::try_lock_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	if( not m_mutex )
		co_return false;

	else if( m_owns )
		co_return true;

	m_owns = co_await m_mutex->try_lock_for (
		std::forward<decltype(exec)>(exec), timeout
	);
	co_return m_owns;
}

template <typename Mutex>
template<typename Clock, typename Duration>
awaitable<bool> unique_lock<Mutex>::try_lock_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	if( not m_mutex )
		co_return false;

	else if( m_owns )
		co_return true;

	m_owns = co_await m_mutex->try_lock_until (
		std::forward<decltype(exec)>(exec), timeout
	);
	co_return m_owns;
}

template <typename Mutex>
template<typename Rep, typename Period>
awaitable<bool> unique_lock<Mutex>::try_lock_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_lock_for (
		co_await asio::this_coro::executor, timeout
	);
}

template <typename Mutex>
template<typename Clock, typename Duration>
awaitable<bool> unique_lock<Mutex>::try_lock_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_lock_until (
		co_await asio::this_coro::executor, timeout
	);
}

template <typename Mutex>
bool unique_lock<Mutex>::is_locked() const noexcept
{
	return m_owns;
}

template <typename Mutex>
unique_lock<Mutex>::mutex_t *unique_lock<Mutex>::mutex() noexcept
{
	return m_mutex;
}

} //namespace riwo::coro


#endif //RIWO_CORO_DETAIL_MUTEX_H
