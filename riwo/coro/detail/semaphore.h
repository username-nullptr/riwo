// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_SEMAPHORE_H
#define RIWO_CORO_DETAIL_SEMAPHORE_H

#include <riwo/coro/detail/wake_up.h>
#include <deque>
#include <mutex>

namespace riwo::coro
{

template<size_t Max>
class RIWO_CORO_TAPI basic_semaphore<Max>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	using wake_up_t = detail::lock_wake_up;

	explicit impl(size_t initial_count) :
		m_counter(initial_count)
	{
		if( initial_count > max_v )
		{
			invalid_argument::loc_throw (
				"riwo::basic_semaphore: Initial count is greater than max value."
			);
		}
	}

	~impl() = default;

public:
	[[nodiscard]] bool try_acquire()
	{
		auto counter = m_counter.load(std::memory_order_relaxed);
		while( counter != 0 )
		{
			if( m_counter.compare_exchange_weak(counter, counter - 1,
				std::memory_order_acquire, std::memory_order_relaxed) )
				return true;
		}
		return false;
	}

	void enqueue(wake_up_t::ptr_t waiter)
	{
		bool acquired = false;
		{
			// Close the failed-fast-path/enqueue gap against release().
			std::lock_guard guard(m_wait_mutex);
			if( try_acquire() )
				acquired = true;
			else
				m_wait_queue.emplace_back(waiter);
		}
		if( acquired )
			(*waiter)(true);
	}

	void enqueue_timed(wake_up_t::ptr_t waiter, const auto &timeout)
	{
		bool acquired = false;
		{
			// Close the failed-fast-path/enqueue gap against release().
			std::lock_guard guard(m_wait_mutex);
			if( try_acquire() )
				acquired = true;
			else
			{
				m_wait_queue.emplace_back(waiter);
				waiter->start_timer(timeout);
			}
		}
		if( acquired )
			(*waiter)(true);
	}

	[[nodiscard]] awaitable<bool> try_acquire_x
	(concepts::sched auto &&exec, auto timeout)
	{
		if( try_acquire() )
			co_return true;

		co_return co_await async_work<bool>::handle(exec,
		[this, timeout, wait_exec = get_executor_helper(exec)]
		(async_work<bool>::handler_t wake_up) mutable
		{
			auto wake_up_ptr = std::make_shared<wake_up_t>(
				wait_exec, std::forward<decltype(wake_up)>(wake_up)
			);
			enqueue_timed(std::move(wake_up_ptr), timeout);
		});
	}

	size_t release(size_t n)
	{
		std::lock_guard guard(m_wait_mutex);
		auto counter = m_counter.load(std::memory_order_relaxed);
		if( n == 0 or n > max_v - counter )
		{
			invalid_argument::loc_throw (
				"riwo::basic_semaphore: Invalid release count."
			);
		}
		while( n-- )
		{
			for(;;)
			{
				if( m_wait_queue.empty() )
				{
					m_counter.fetch_add(1, std::memory_order_release);
					break;
				}
				auto waiter = std::move(m_wait_queue.front());
				m_wait_queue.pop_front();
				if( (*waiter)(true) )
					break;
			}
		}
		return m_counter.load(std::memory_order_acquire);
	}

	size_t release_binary()
	{
		std::lock_guard guard(m_wait_mutex);
		if( m_counter.load(std::memory_order_relaxed) == 1 )
		{
			runtime_error::loc_throw (
				"riwo::basic_semaphore: Release a binary_semaphore with max count 1 more than once."
			);
		}
		for(;;)
		{
			if( m_wait_queue.empty() )
			{
				m_counter.store(1, std::memory_order_release);
				break;
			}
			auto waiter = std::move(m_wait_queue.front());
			m_wait_queue.pop_front();

			if( (*waiter)(true) )
				break;
		}
		return m_counter.load(std::memory_order_acquire);
	}

public:
	std::atomic_size_t m_counter = 0;
	std::mutex m_wait_mutex;
	std::deque<wake_up_t::ptr_t> m_wait_queue;
};

template<size_t Max>
basic_semaphore<Max>::basic_semaphore(size_t initial_count) :
	m_impl(new impl(initial_count))
{

}

template<size_t Max>
basic_semaphore<Max>::~basic_semaphore()
{
	delete m_impl;
}

template<size_t Max>
awaitable<void> basic_semaphore<Max>::acquire(concepts::sched auto &&exec)
{
	if( try_acquire() )
		co_return ;

	co_await async_work<bool>::handle(exec,
	[this, wait_exec = get_executor_helper(exec)](async_work<bool>::handler_t wake_up) mutable
	{
		m_impl->enqueue(std::make_shared<typename impl::wake_up_t>(
			wait_exec, std::move(wake_up)
		));
	});
	co_return ;
}

template<size_t Max>
awaitable<void> basic_semaphore<Max>::acquire()
{
	co_return co_await acquire (
		co_await asio::this_coro::executor
	);
}

template<size_t Max>
bool basic_semaphore<Max>::try_acquire()
{
	return m_impl->try_acquire();
}

template<size_t Max>
size_t basic_semaphore<Max>::release(size_t n) requires (max_v > 1)
{
	return m_impl->release(n);
}

template<size_t Max>
size_t basic_semaphore<Max>::release() requires (max_v == 1)
{
	return m_impl->release_binary();
}

template<size_t Max>
template<typename Rep, typename Period>
awaitable<bool> basic_semaphore<Max>::try_acquire_for
(concepts::sched auto &&exec, const duration<Rep,Period> &timeout)
{
	return m_impl->try_acquire_x(std::forward<decltype(exec)>(exec),
		std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
	);
}

template<size_t Max>
template<typename Clock, typename Duration>
awaitable<bool> basic_semaphore<Max>::try_acquire_until
(concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout)
{
	return m_impl->try_acquire_x(std::forward<decltype(exec)>(exec), timeout);
}

template<size_t Max>
template<typename Rep, typename Period>
awaitable<bool> basic_semaphore<Max>::try_acquire_for(const duration<Rep,Period> &timeout)
{
	co_return co_await try_acquire_for (
		co_await asio::this_coro::executor, timeout
	);
}

template<size_t Max>
template<typename Clock, typename Duration>
awaitable<bool> basic_semaphore<Max>::try_acquire_until(const time_point<Clock,Duration> &timeout)
{
	co_return co_await try_acquire_until (
		co_await asio::this_coro::executor, timeout
	);
}

template<size_t Max>
consteval size_t basic_semaphore<Max>::max() const noexcept
{
	return max_v;
}

template<size_t Max>
size_t basic_semaphore<Max>::count() const noexcept
{
	return m_impl->m_counter.load(std::memory_order_acquire);
}

} //namespace riwo::coro


#endif //RIWO_CORO_DETAIL_SEMAPHORE_H
