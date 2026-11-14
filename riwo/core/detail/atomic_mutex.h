// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_ATOMIC_MUTEX_H
#define RIWO_CORE_DETAIL_ATOMIC_MUTEX_H

namespace riwo { namespace detail
{

inline void none_instruction() noexcept
{
#ifdef _MSC_VER
	_mm_pause();
#elif defined(__GNUC__)
# if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	asm volatile("pause" : : : "memory");
# elif defined(__aarch64__) || defined(__arm64__) || defined(__arm__)
	asm volatile("yield" : : : "memory");
# elif defined(__powerpc__) || defined(__ppc__)
	asm volatile("or 0, 0, 0" : : : "memory");
# elif defined(__riscv)
	asm volatile("fence w, 0" : : : "memory");
# else // Unknown
	asm volatile("" : : : "memory");
# endif // CPU Architecture
#endif //_MSC_VER
}

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
				detail::none_instruction();

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
