// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "mutex.h"
#include <riwo/coro/detail/wake_up.h>
#include <deque>
#include <mutex>

namespace riwo::coro { namespace detail
{

class mutex_impl
{
	RIWO_DISABLE_COPY_MOVE(mutex_impl)

public:
	using native_handle_t = mutex::native_handle_t;
	using wake_up_t = lock_wake_up;

	mutex_impl() = default;
	~mutex_impl() = default;

	[[nodiscard]] bool try_lock()
	{
		bool flag = false;
		return m_native_handle.compare_exchange_strong(flag, true,
			std::memory_order_acquire, std::memory_order_relaxed
		);
	}

	void enqueue(const wake_up_t::ptr_t &waiter)
	{
		bool acquired = false;
		{
			std::lock_guard guard(m_wait_mutex);
			if( try_lock() )
				acquired = true;
			else
				m_wait_queue.emplace_back(waiter);
		}
		if( acquired )
			(*waiter)(true);
	}

	void enqueue_timed(wake_up_t::ptr_t waiter, asio::steady_timer::duration timeout)
	{
		bool acquired = false;
		{
			std::lock_guard guard(m_wait_mutex);
			if( try_lock() )
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

	void enqueue_timed(wake_up_t::ptr_t waiter, asio::steady_timer::time_point timeout)
	{
		bool acquired = false;
		{
			std::lock_guard guard(m_wait_mutex);
			if( try_lock() )
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

	void unlock()
	{
		for(;;)
		{
			wake_up_t::ptr_t waiter;
			{
				std::lock_guard guard(m_wait_mutex);
				if( m_wait_queue.empty() )
				{
					m_native_handle.store(false, std::memory_order_release);
					return ;
				}
				waiter = std::move(m_wait_queue.front());
				m_wait_queue.pop_front();
			}
			if( (*waiter)(true) )
				return ;
		}
	}

	[[nodiscard]] awaitable<void> lock(asio::any_io_executor exec)
	{
		if( try_lock() )
			co_return ;

		co_await async_work<bool>::handle(exec,
		[this, exec](async_work<bool>::handler_t wake_up) mutable
		{
			enqueue(std::make_shared<wake_up_t>(exec, std::move(wake_up)));
		});
		co_return ;
	}

	[[nodiscard]] awaitable<bool> try_lock_for
	(asio::any_io_executor exec, asio::steady_timer::duration timeout)
	{
		if( try_lock() )
			co_return true;

		co_return co_await async_work<bool>::handle(exec,
		[this, timeout, exec](async_work<bool>::handler_t wake_up) mutable
		{
			auto waiter = std::make_shared<wake_up_t>(exec, std::move(wake_up));
			enqueue_timed(std::move(waiter), timeout);
		});
	}

	[[nodiscard]] awaitable<bool> try_lock_until
	(asio::any_io_executor exec, asio::steady_timer::time_point timeout)
	{
		if( try_lock() )
			co_return true;

		co_return co_await async_work<bool>::handle(exec,
		[this, timeout, exec](async_work<bool>::handler_t wake_up) mutable
		{
			auto waiter = std::make_shared<wake_up_t>(exec, std::move(wake_up));
			enqueue_timed(std::move(waiter), timeout);
		});
	}

	[[nodiscard]] bool is_locked() const noexcept
	{
		return m_native_handle.load(std::memory_order_acquire);
	}

	[[nodiscard]] native_handle_t &native_handle() noexcept
	{
		return m_native_handle;
	}

private:
	native_handle_t m_native_handle {false};
	std::mutex m_wait_mutex;
	std::deque<wake_up_t::ptr_t> m_wait_queue;
};

awaitable<void> mutex_lock(mutex_impl *impl, asio::any_io_executor exec)
{
	return impl->lock(std::move(exec));
}

awaitable<bool> mutex_try_lock_for
(mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::duration timeout)
{
	return impl->try_lock_for(std::move(exec), timeout);
}

awaitable<bool> mutex_try_lock_until
(mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::time_point timeout)
{
	return impl->try_lock_until(std::move(exec), timeout);
}

} //namespace detail

mutex::mutex() :
	m_impl(new detail::mutex_impl())
{

}

mutex::~mutex()
{
	delete m_impl;
}

awaitable<void> mutex::lock()
{
	co_return co_await detail::mutex_lock(
		m_impl, co_await asio::this_coro::executor
	);
}

bool mutex::try_lock()
{
	return m_impl->try_lock();
}

void mutex::unlock()
{
	m_impl->unlock();
}

bool mutex::is_locked() const noexcept
{
	return m_impl->is_locked();
}

mutex::native_handle_t &mutex::native_handle() noexcept
{
	return m_impl->native_handle();
}

} //namespace riwo::coro
