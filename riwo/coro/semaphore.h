// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_SEMAPHORE_H
#define RIWO_CORO_SEMAPHORE_H

#include <riwo/coro/global.h>

namespace riwo::coro
{

template<size_t Max = std::numeric_limits<size_t>::max()>
class RIWO_CORO_TAPI basic_semaphore
{
	RIWO_DISABLE_COPY_MOVE(basic_semaphore)
	constexpr static size_t max_v = Max;

	static_assert(max_v > 0,
		"Max must be greater than 0"
	);
public:
	explicit basic_semaphore(size_t initial_count = max_v);
	~basic_semaphore();

public:
	[[nodiscard]] awaitable<void> acquire(concepts::sched auto &&exec);
	[[nodiscard]] awaitable<void> acquire();

	[[nodiscard]] bool try_acquire();
	size_t release(size_t n = 1) requires (max_v > 1);
	size_t release() requires (max_v == 1);

public:
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_acquire_for (
		concepts::sched auto &&exec, const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_acquire_until (
		concepts::sched auto &&exec, const time_point<Clock,Duration> &timeout
	);
	template<typename Rep, typename Period>
	[[nodiscard]] awaitable<bool> try_acquire_for (
		const duration<Rep,Period> &timeout
	);
	template<typename Clock, typename Duration>
	[[nodiscard]] awaitable<bool> try_acquire_until (
		const time_point<Clock,Duration> &timeout
	);

public:
	[[nodiscard]] consteval size_t max() const noexcept;
	[[nodiscard]] size_t count() const noexcept;

private:
	class impl;
	impl *m_impl;
};

using binary_semaphore = basic_semaphore<1>;
using semaphore = basic_semaphore<>;

} //namespace riwo::coro
#include <riwo/coro/detail/semaphore.h>


#endif //RIWO_CORO_SEMAPHORE_H
