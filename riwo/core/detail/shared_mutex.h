// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_SHARED_MUTEX_H
#define RIWO_CORE_DETAIL_SHARED_MUTEX_H

#include <riwo/core/atomic_mutex.h>
#include <shared_mutex>

namespace riwo
{

template <atomic_mutex_policy Policy>
void basic_atomic_shared_mutex<Policy>::lock() noexcept
{
	if( state_t expected = 0;
		m_writer_state.compare_exchange_strong(expected, writer_active,
			std::memory_order_acquire, std::memory_order_relaxed) )
	{
		wait_for_readers();
		return ;
	}
	auto state = m_writer_state.fetch_add(1, std::memory_order_relaxed) + 1;
	for(;;)
	{
		if( (state & writer_active) == 0 )
		{
			if( const auto desired = (state - 1) | writer_active;
				m_writer_state.compare_exchange_weak(state, desired,
					std::memory_order_acquire, std::memory_order_relaxed) )
			{
				wait_for_readers();
				return ;
			}
			continue;
		}
		if constexpr( Policy == atomic_mutex_policy::low_latency )
		{
			detail::spin_while_equal(m_writer_state, state);
			state = m_writer_state.load(std::memory_order_relaxed);
		}
		else
		{
			m_writer_state.wait(state, std::memory_order_relaxed);
			state = m_writer_state.load(std::memory_order_relaxed);
		}
	}
}

template <atomic_mutex_policy Policy>
bool basic_atomic_shared_mutex<Policy>::try_lock() noexcept
{
	if( state_t expected = 0;
		not m_writer_state.compare_exchange_strong(expected, writer_active,
			std::memory_order_acquire, std::memory_order_relaxed) )
		return false;

	if( m_reader_count.load(std::memory_order_acquire) == 0 )
		return true;

	// A writer may have queued after the compare-exchange above.  Reuse the
	// normal unlock path so its waiter count is preserved; storing zero here
	// would lose that count and could leave a phantom writer state forever.
	unlock();
	return false;
}

template <atomic_mutex_policy Policy>
void basic_atomic_shared_mutex<Policy>::unlock() noexcept
{
	const auto previous = m_writer_state.fetch_and(
		~writer_active, std::memory_order_release
	);
	if constexpr( Policy == atomic_mutex_policy::balanced )
	{
		if( (previous & writer_waiter_mask) != 0 )
			m_writer_state.notify_one();
		else
		{
			m_reader_epoch.fetch_add(1, std::memory_order_relaxed);
			m_reader_epoch.notify_all();
		}
	}
}

template <atomic_mutex_policy Policy>
void basic_atomic_shared_mutex<Policy>::lock_shared() noexcept
{
	auto state = m_writer_state.load(std::memory_order_acquire);
	for(;;)
	{
		if( state == 0 )
		{
			m_reader_count.fetch_add(1, std::memory_order_relaxed);
			if( m_writer_state.load(std::memory_order_acquire) == 0 )
				return ;

			const auto rollback = m_reader_count.fetch_sub(
				1, std::memory_order_release
			);
			if constexpr( Policy == atomic_mutex_policy::balanced )
			{
				if( rollback == 1 )
					m_reader_count.notify_one();
			}
			state = m_writer_state.load(std::memory_order_acquire);
			continue;
		}
		if constexpr( Policy == atomic_mutex_policy::low_latency )
		{
			detail::spin_while_equal(m_writer_state, state);
			state = m_writer_state.load(std::memory_order_acquire);
		}
		else
		{
			const auto epoch = m_reader_epoch.load(std::memory_order_relaxed);
			state = m_writer_state.load(std::memory_order_acquire);

			if( state != 0 )
				m_reader_epoch.wait(epoch, std::memory_order_relaxed);

			state = m_writer_state.load(std::memory_order_acquire);
		}
	}
}

template <atomic_mutex_policy Policy>
bool basic_atomic_shared_mutex<Policy>::try_lock_shared() noexcept
{
	if( m_writer_state.load(std::memory_order_acquire) != 0 )
		return false;

	m_reader_count.fetch_add(1, std::memory_order_relaxed);
	if( m_writer_state.load(std::memory_order_acquire) == 0 )
		return true;

	const auto rollback = m_reader_count.fetch_sub(1, std::memory_order_release);
	if constexpr( Policy == atomic_mutex_policy::balanced )
	{
		if( rollback == 1 )
			m_reader_count.notify_one();
	}
	return false;
}

template <atomic_mutex_policy Policy>
void basic_atomic_shared_mutex<Policy>::unlock_shared() noexcept
{
	const auto previous = m_reader_count.fetch_sub(1, std::memory_order_release);
	if constexpr( Policy == atomic_mutex_policy::balanced )
	{
		if( previous == 1 )
			m_reader_count.notify_one();
	}
}

template <atomic_mutex_policy Policy>
void basic_atomic_shared_mutex<Policy>::wait_for_readers() noexcept
{
	auto readers = m_reader_count.load(std::memory_order_acquire);
	while( readers != 0 )
	{
		if constexpr( Policy == atomic_mutex_policy::low_latency )
			detail::spin_while_equal(m_reader_count, readers);
		else
			m_reader_count.wait(readers, std::memory_order_relaxed);
		readers = m_reader_count.load(std::memory_order_acquire);
	}
}

} //namesapace riwo


#endif //RIWO_CORE_DETAIL_SHARED_MUTEX_H
