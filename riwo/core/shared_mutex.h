// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SHARED_MUTEX_H
#define RIWO_CORE_SHARED_MUTEX_H

#include <riwo/core/atomic_mutex.h>
#include <shared_mutex>
#include <mutex>

namespace riwo
{

using shared_mutex = std::shared_mutex;
using shared_timed_mutex = std::shared_timed_mutex;

using shared_shared_lock = std::shared_lock<shared_mutex>;
using shared_shared_timed_lock = std::shared_lock<shared_timed_mutex>;

using shared_unique_lock = std::unique_lock<shared_mutex>;
using shared_unique_timed_lock = std::unique_lock<shared_timed_mutex>;

template <atomic_mutex_policy = atomic_mutex_policy::balanced>
class RIWO_CORE_VAPI basic_atomic_shared_mutex
{
	RIWO_DISABLE_COPY_MOVE(basic_atomic_shared_mutex)

public:
	basic_atomic_shared_mutex() = default;
	~basic_atomic_shared_mutex() = default;

public:
	void lock() noexcept;
	[[nodiscard]] bool try_lock() noexcept;
	void unlock() noexcept;

public:
	void lock_shared() noexcept;
	[[nodiscard]] bool try_lock_shared() noexcept;
	void unlock_shared() noexcept;

private:
	void wait_for_readers() noexcept;

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4324)
#endif
	using state_t = uint32_t;
	static constexpr auto state_bits = sizeof(state_t) * 8;

	static constexpr state_t writer_active = state_t {1} << (state_bits - 1);
	static constexpr state_t writer_waiter_mask = ~writer_active;

	std::atomic<state_t> m_writer_state {0};
	std::atomic_uint32_t m_reader_epoch {0};
	alignas(64) std::atomic_size_t m_reader_count {0};
#ifdef _MSC_VER
# pragma warning(pop)
#endif
};

using atomic_shared_mutex = basic_atomic_shared_mutex<>;
using spin_shared_mutex = basic_atomic_shared_mutex<atomic_mutex_policy::low_latency>;

template <atomic_mutex_policy Policy = atomic_mutex_policy::balanced>
using basic_atomic_shared_lock = std::shared_lock<basic_atomic_shared_mutex<Policy>>;

template <atomic_mutex_policy Policy = atomic_mutex_policy::balanced>
using basic_atomic_shared_unique_lock = std::unique_lock<basic_atomic_shared_mutex<Policy>>;

using atomic_shared_lock = basic_atomic_shared_lock<>;
using atomic_shared_unique_lock = basic_atomic_shared_unique_lock<>;

using spin_shared_lock = basic_atomic_shared_lock<atomic_mutex_policy::low_latency>;
using spin_shared_unique_lock = basic_atomic_shared_unique_lock<atomic_mutex_policy::low_latency>;

} //namesapace riwo
#include <riwo/core/detail/shared_mutex.h>


#endif //RIWO_CORE_SHARED_MUTEX_H
