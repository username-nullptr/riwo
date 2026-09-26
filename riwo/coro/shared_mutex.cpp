// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "shared_mutex.h"

namespace riwo::coro { namespace detail
{

class RIWO_CORO_API shared_mutex_impl
{
	RIWO_DISABLE_COPY_MOVE(shared_mutex_impl)

public:
	using native_handle_t = shared_mutex::native_handle_t;

	shared_mutex_impl() = default;
	~shared_mutex_impl() = default;

	[[nodiscard]] awaitable<void> lock(asio::any_io_executor exec)
	{
		return m_native_handle.lock(std::move(exec));
	}

	[[nodiscard]] awaitable<void> lock_shared(asio::any_io_executor exec)
	{
		co_await m_read_gate.lock(exec);
		if( not try_join_readers() )
		{
			co_await m_native_handle.lock(exec);
			start_reading();
		}
		m_read_gate.unlock();
		co_return ;
	}

	[[nodiscard]] awaitable<bool> try_lock_for
	(asio::any_io_executor exec, asio::steady_timer::duration timeout) {
		return m_native_handle.try_lock_for(std::move(exec), timeout);
	}

	[[nodiscard]] awaitable<bool> try_lock_until
	(asio::any_io_executor exec, asio::steady_timer::time_point timeout) {
		return m_native_handle.try_lock_until(std::move(exec), timeout);
	}

	[[nodiscard]] awaitable<bool> try_lock_shared_for
	(asio::any_io_executor exec, asio::steady_timer::duration timeout)
	{
		return try_lock_shared_until(std::move(exec),
			asio::steady_timer::clock_type::now() + timeout
		);
	}

	[[nodiscard]] awaitable<bool> try_lock_shared_until
	(asio::any_io_executor exec, asio::steady_timer::time_point timeout)
	{
		if( not co_await m_read_gate.try_lock_until(exec, timeout) )
			co_return false;

		if( not try_join_readers() )
		{
			if( not co_await m_native_handle.try_lock_until(exec, timeout) )
			{
				m_read_gate.unlock();
				co_return false;
			}
			start_reading();
		}
		m_read_gate.unlock();
		co_return true;
	}

	[[nodiscard]] bool try_lock() {
		return m_native_handle.try_lock();
	}

	void unlock() {
		m_native_handle.unlock();
	}

	[[nodiscard]] bool try_lock_shared()
	{
		if( not m_read_gate.try_lock() )
			return false;

		if( not try_join_readers() )
		{
			if( not m_native_handle.try_lock() )
			{
				m_read_gate.unlock();
				return false;
			}
			start_reading();
		}
		m_read_gate.unlock();
		return true;
	}

	void unlock_shared()
	{
		auto readers = m_read_count.load(std::memory_order_relaxed);
		while( readers != 0 )
		{
			if( m_read_count.compare_exchange_weak(readers, readers - 1,
				std::memory_order_acq_rel, std::memory_order_relaxed) )
			{
				if( readers == 1 )
					m_native_handle.unlock();
				return ;
			}
		}
	}

	[[nodiscard]] bool is_locked() const noexcept {
		return m_native_handle.is_locked();
	}

	[[nodiscard]] native_handle_t &native_handle() noexcept {
		return m_native_handle;
	}

private:
	[[nodiscard]] bool try_join_readers()
	{
		auto readers = m_read_count.load(std::memory_order_acquire);
		while( readers != 0 )
		{
			if( m_read_count.compare_exchange_weak(readers, readers + 1,
				std::memory_order_acquire, std::memory_order_relaxed) )
				return true;
		}
		return false;
	}

	void start_reading()
	{
		// m_native_handle and m_read_gate make this the only zero-to-one
		// transition.  Publishing it with release lets joining readers inherit
		// the writer-to-reader synchronization without another mutex.
		m_read_count.store(1, std::memory_order_release);
	}

private:
	std::atomic_uint m_read_count {0};
	native_handle_t m_native_handle;
	mutex m_read_gate;
};

class shared_lock_impl
{
	RIWO_DISABLE_COPY_MOVE(shared_lock_impl)

public:
	explicit shared_lock_impl(shared_mutex &mutex) :
		m_mutex(&mutex) {}

	shared_mutex *m_mutex;
	bool m_owns = false;
};

awaitable<void> shared_mutex_lock
(shared_mutex_impl *impl, asio::any_io_executor exec)
{
	return impl->lock(std::move(exec));
}

awaitable<void> shared_mutex_lock_shared
(shared_mutex_impl *impl, asio::any_io_executor exec)
{
	return impl->lock_shared(std::move(exec));
}

awaitable<bool> shared_mutex_try_lock_for
(shared_mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::duration timeout)
{
	return impl->try_lock_for(std::move(exec), timeout);
}

awaitable<bool> shared_mutex_try_lock_until
(shared_mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::time_point timeout)
{
	return impl->try_lock_until(std::move(exec), timeout);
}

awaitable<bool> shared_mutex_try_lock_shared_for
(shared_mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::duration timeout)
{
	return impl->try_lock_shared_for(std::move(exec), timeout);
}

awaitable<bool> shared_mutex_try_lock_shared_until
(shared_mutex_impl *impl, asio::any_io_executor exec, asio::steady_timer::time_point timeout)
{
	return impl->try_lock_shared_until(std::move(exec), timeout);
}

awaitable<void> shared_lock_lock_shared
(shared_lock_impl *impl, asio::any_io_executor exec)
{
	if( impl and impl->m_mutex and not impl->m_owns )
	{
		co_await impl->m_mutex->lock_shared(std::move(exec));
		impl->m_owns = true;
	}
	co_return ;
}

awaitable<bool> shared_lock_try_lock_shared_for
(shared_lock_impl *impl, asio::any_io_executor exec, asio::steady_timer::duration timeout)
{
	if( not impl or not impl->m_mutex )
		co_return false;

	else if( impl->m_owns )
		co_return true;

	impl->m_owns = co_await impl->m_mutex->try_lock_shared_for (
		std::move(exec), timeout
	);
	co_return impl->m_owns;
}

awaitable<bool> shared_lock_try_lock_shared_until
(shared_lock_impl *impl, asio::any_io_executor exec, asio::steady_timer::time_point timeout)
{
	if( not impl or not impl->m_mutex )
		co_return false;

	else if( impl->m_owns )
		co_return true;

	impl->m_owns = co_await impl->m_mutex->try_lock_shared_until (
		std::move(exec), timeout
	);
	co_return impl->m_owns;
}

} //namespace detail

shared_mutex::shared_mutex() :
	m_impl(new detail::shared_mutex_impl())
{

}

shared_mutex::~shared_mutex()
{
	delete m_impl;
}

awaitable<void> shared_mutex::lock()
{
	co_return co_await detail::shared_mutex_lock (
		m_impl, co_await asio::this_coro::executor
	);
}

bool shared_mutex::try_lock()
{
	return m_impl->try_lock();
}

void shared_mutex::unlock()
{
	m_impl->unlock();
}

awaitable<void> shared_mutex::lock_shared()
{
	co_return co_await detail::shared_mutex_lock_shared (
		m_impl, co_await asio::this_coro::executor
	);
}

bool shared_mutex::try_lock_shared()
{
	return m_impl->try_lock_shared();
}

void shared_mutex::unlock_shared()
{
	m_impl->unlock_shared();
}

bool shared_mutex::is_locked() const noexcept
{
	return m_impl->is_locked();
}

shared_mutex::native_handle_t &shared_mutex::native_handle() noexcept
{
	return m_impl->native_handle();
}

shared_lock::shared_lock(mutex_t &mutex) :
	m_impl(new detail::shared_lock_impl(mutex))
{

}

shared_lock::~shared_lock() noexcept(false)
{
	if( m_impl )
	{
		if( m_impl->m_mutex and m_impl->m_owns )
		{
			m_impl->m_owns = false;
			m_impl->m_mutex->unlock_shared();
		}
		delete m_impl;
	}
}

shared_lock::shared_lock(shared_lock &&other) noexcept :
	m_impl(std::exchange(other.m_impl, nullptr))
{

}

shared_lock &shared_lock::operator=(shared_lock &&other) noexcept
{
	if( this == &other )
		return *this;

	if( m_impl )
	{
		if( m_impl->m_mutex and m_impl->m_owns )
		{
			m_impl->m_owns = false;
			m_impl->m_mutex->unlock_shared();
		}
		delete m_impl;
	}
	m_impl = std::exchange(other.m_impl, nullptr);
	return *this;
}

awaitable<void> shared_lock::lock_shared()
{
	co_return co_await detail::shared_lock_lock_shared (
		m_impl, co_await asio::this_coro::executor
	);
}

bool shared_lock::try_lock_shared()
{
	if( not m_impl or not m_impl->m_mutex )
		return false;

	else if( m_impl->m_owns )
		return true;

	return m_impl->m_owns = m_impl->m_mutex->try_lock_shared();
}

void shared_lock::unlock_shared()
{
	if( m_impl and m_impl->m_mutex and m_impl->m_owns )
	{
		m_impl->m_owns = false;
		m_impl->m_mutex->unlock_shared();
	}
}

bool shared_lock::is_locked() const noexcept
{
	return m_impl and m_impl->m_owns;
}

shared_lock::mutex_t *shared_lock::mutex() noexcept
{
	return m_impl ? m_impl->m_mutex : nullptr;
}

} //namespace riwo::coro
