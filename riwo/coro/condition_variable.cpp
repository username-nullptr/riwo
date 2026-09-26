// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "condition_variable.h"
#include <riwo/coro/detail/wake_up.h>

namespace riwo::coro { namespace detail
{

class condition_variable_impl
{
	RIWO_DISABLE_COPY_MOVE(condition_variable_impl)

public:
	condition_variable_impl() = default;
	~condition_variable_impl() = default;

	[[nodiscard]] awaitable<bool> wait(asio::any_io_executor exec, std::function<void()> unlock)
	{
		co_return co_await async_work<bool>::handle(exec,
		[this, unlock = std::move(unlock), exec](async_work<bool>::handler_t wake_up) mutable
		{
			auto waiter = std::make_shared<lock_wake_up>(exec, std::move(wake_up));
			std::lock_guard guard(m_mutex);
			m_wait_queue.emplace_back(std::move(waiter));
			unlock();
		});
	}

	[[nodiscard]] awaitable<bool> wait
	(asio::any_io_executor exec, std::function<void()> unlock, asio::steady_timer::duration timeout)
	{
		co_return co_await async_work<bool>::handle(exec,
		[this, unlock = std::move(unlock), timeout, exec]
		(async_work<bool>::handler_t wake_up) mutable
		{
			auto waiter = std::make_shared<lock_wake_up>(exec, std::move(wake_up));
			std::lock_guard guard(m_mutex);
			m_wait_queue.emplace_back(waiter);
			waiter->start_timer(timeout);
			unlock();
		});
	}

	[[nodiscard]] awaitable<bool> wait
	(asio::any_io_executor exec, std::function<void()> unlock, asio::steady_timer::time_point timeout)
	{
		co_return co_await async_work<bool>::handle(exec,
		[this, unlock = std::move(unlock), timeout, exec]
		(async_work<bool>::handler_t wake_up) mutable
		{
			auto waiter = std::make_shared<lock_wake_up>(exec, std::move(wake_up));
			std::lock_guard guard(m_mutex);
			m_wait_queue.emplace_back(waiter);
			waiter->start_timer(timeout);
			unlock();
		});
	}

	void notify_one() noexcept
	{
		for(;;)
		{
			lock_wake_up_ptr waiter;
			{
				std::lock_guard guard(m_mutex);
				if( m_wait_queue.empty() )
					return ;

				waiter = std::move(m_wait_queue.front());
				m_wait_queue.pop_front();
			}
			if( (*waiter)(true) )
				return ;
		}
	}

	void notify_all() noexcept
	{
		std::deque<lock_wake_up_ptr> waiters;
		{
			std::lock_guard guard(m_mutex);
			waiters.swap(m_wait_queue);
		}
		for(auto &waiter : waiters)
			(*waiter)(true);
	}

private:
	std::deque<lock_wake_up_ptr> m_wait_queue;
	std::mutex m_mutex;
};

awaitable<bool> condition_variable_wait
(condition_variable_impl *impl, asio::any_io_executor exec, std::function<void()> unlock)
{
	return impl->wait(std::move(exec), std::move(unlock));
}

awaitable<bool> condition_variable_wait(condition_variable_impl *impl,
	asio::any_io_executor exec, std::function<void()> unlock, asio::steady_timer::duration timeout)
{
	return impl->wait(std::move(exec), std::move(unlock), timeout);
}

awaitable<bool> condition_variable_wait(condition_variable_impl *impl,
	asio::any_io_executor exec, std::function<void()> unlock, asio::steady_timer::time_point timeout)
{
	return impl->wait(std::move(exec), std::move(unlock), timeout);
}

} //namespace detail

condition_variable::condition_variable() :
	m_impl(new detail::condition_variable_impl())
{

}

condition_variable::~condition_variable()
{
	delete m_impl;
}

void condition_variable::notify_one() noexcept
{
	m_impl->notify_one();
}

void condition_variable::notify_all() noexcept
{
	m_impl->notify_all();
}

} //namespace riwo::coro
