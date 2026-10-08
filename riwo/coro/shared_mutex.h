// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_SHARED_MUTEX_H
#define RIWO_CORO_SHARED_MUTEX_H

#include <riwo/coro/mutex.h>

namespace riwo::coro { namespace detail
{

class shared_mutex_impl;
class shared_lock_impl;

} //namespace detail

class RIWO_CORO_API shared_mutex
{
	RIWO_DISABLE_COPY_MOVE(shared_mutex)

public:
	using native_handle_t = mutex;
	shared_mutex();
	~shared_mutex();

public:
	[[nodiscard]] awaitable<void> lock(concepts::sched auto &&exec);
	[[nodiscard]] awaitable<void> lock();

	[[nodiscard]] bool try_lock();
	void unlock();

public:
	[[nodiscard]] awaitable<void> lock_shared(concepts::sched auto &&exec);
	[[nodiscard]] awaitable<void> lock_shared();

	[[nodiscard]] bool try_lock_shared();
	void unlock_shared();

public:
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_for (
		concepts::sched auto &&exec, const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_until (
		concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout
	);
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_for (
		const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_until (
		const time_point<Clock,Duration> &timeout
	);

public:
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_shared_for (
		concepts::sched auto &&exec, const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_shared_until (
		concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout
	);
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_shared_for (
		const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_shared_until (
		const time_point<Clock,Duration> &timeout
	);

public:
	[[nodiscard]] bool is_locked() const noexcept;
	[[nodiscard]] native_handle_t &native_handle() noexcept;

private:
	detail::shared_mutex_impl *m_impl;
};

class RIWO_CORO_API shared_lock
{
	RIWO_DISABLE_COPY(shared_lock)

public:
	using mutex_t = shared_mutex;

	explicit shared_lock(mutex_t &mutex);
	~shared_lock() noexcept(false);

	shared_lock(shared_lock &&other) noexcept;
	shared_lock &operator=(shared_lock &&other) noexcept;

public:
	[[nodiscard]] awaitable<void> lock_shared(concepts::sched auto &&exec);
	[[nodiscard]] awaitable<void> lock_shared();

	[[nodiscard]] bool try_lock_shared();
	void unlock_shared();

public:
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_shared_for (
		concepts::sched auto &&exec, const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_shared_until (
		concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout
	);
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_lock_shared_for (
		const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_lock_shared_until (
		const time_point<Clock,Duration> &timeout
	);

public:
	[[nodiscard]] bool is_locked() const noexcept;
	[[nodiscard]] mutex_t *mutex() noexcept;

private:
	detail::shared_lock_impl *m_impl;
};

using shared_unique_lock = unique_lock<shared_mutex>;

} //namespace riwo::coro
#include <riwo/coro/detail/shared_mutex.h>


#endif //RIWO_CORO_SHARED_MUTEX_H
