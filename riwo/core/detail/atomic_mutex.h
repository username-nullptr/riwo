// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_ATOMIC_MUTEX_H
#define RIWO_CORE_DETAIL_ATOMIC_MUTEX_H

#include <riwo/core/system/cpu.h>


namespace riwo { namespace detail
{

template <typename T>
void spin_while_equal(std::atomic<T> &value, T expected) noexcept
{
	while( value.load(std::memory_order_relaxed) == expected )
		none_instruction();
}

} //namespace detail

template <atomic_mutex_policy Policy>
void basic_atomic_mutex<Policy>::lock() noexcept
{
	uint32_t expected = unlocked;
	if( m_native_handle.compare_exchange_strong(expected, locked,
		std::memory_order_acquire, std::memory_order_relaxed) )
		return ;

	if constexpr( Policy == atomic_mutex_policy::low_latency )
	{
		for(;;)
		{
			while( m_native_handle.load(std::memory_order_relaxed) != unlocked )
				none_instruction();

			expected = unlocked;
			if( m_native_handle.compare_exchange_weak(expected, locked,
				std::memory_order_acquire, std::memory_order_relaxed) )
				return ;
		}
	}
	else
	{
		while( m_native_handle.exchange(contended, std::memory_order_acquire) != unlocked )
			m_native_handle.wait(contended, std::memory_order_relaxed);
	}
}

template <atomic_mutex_policy Policy>
bool basic_atomic_mutex<Policy>::try_lock() noexcept
{
	uint32_t expected = unlocked;
	return m_native_handle.compare_exchange_strong(expected, locked,
		std::memory_order_acquire, std::memory_order_relaxed
	);
}

template <atomic_mutex_policy Policy>
void basic_atomic_mutex<Policy>::unlock() noexcept
{
	if constexpr( Policy == atomic_mutex_policy::low_latency )
		m_native_handle.store(unlocked, std::memory_order_release);

	else if( m_native_handle.exchange(unlocked, std::memory_order_release) == contended )
		m_native_handle.notify_one();
}

template <atomic_mutex_policy Policy>
auto basic_atomic_mutex<Policy>::native_handle() noexcept -> native_handle_t&
{
	return m_native_handle;
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_ATOMIC_MUTEX_H
