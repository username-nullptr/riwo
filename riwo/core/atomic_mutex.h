// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ATOMIC_MUTEX_H
#define RIWO_CORE_ATOMIC_MUTEX_H

#include <riwo/core/global.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace riwo
{

enum class atomic_mutex_policy {
	balanced, low_latency, spin = low_latency
};

template <atomic_mutex_policy = atomic_mutex_policy::balanced>
class RIWO_CORE_VAPI basic_atomic_mutex
{
	RIWO_DISABLE_COPY_MOVE(basic_atomic_mutex)

public:
	using native_handle_t = std::atomic_uint32_t;

	basic_atomic_mutex() = default;
	~basic_atomic_mutex() = default;

public:
	void lock() noexcept;
	[[nodiscard]] bool try_lock() noexcept;
	void unlock() noexcept;

	[[nodiscard]] native_handle_t &native_handle() noexcept;

private:
	static constexpr uint32_t unlocked = 0;
	static constexpr uint32_t locked = 1;
	static constexpr uint32_t contended = 2;
	native_handle_t m_native_handle {unlocked};
};

using atomic_mutex = basic_atomic_mutex<>;
using spin_mutex = basic_atomic_mutex<atomic_mutex_policy::low_latency>;

template <atomic_mutex_policy Policy = atomic_mutex_policy::balanced>
using basic_atomic_unique_lock = std::unique_lock<basic_atomic_mutex<Policy>>;

using atomic_unique_lock = basic_atomic_unique_lock<>;
using spin_unique_lock = basic_atomic_unique_lock<atomic_mutex_policy::low_latency>;

} //namespace riwo
#include <riwo/core/detail/atomic_mutex.h>


#endif //RIWO_CORE_ATOMIC_MUTEX_H
