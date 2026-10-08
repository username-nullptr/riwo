// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_UTILS_H
#define RIWO_CORO_UTILS_H

#include <riwo/coro/global.h>

#if RIWO_USING_BOOST_ASIO
# include <boost/asio/experimental/awaitable_operators.hpp>
#else //RIWO_USING_BOOST_ASIO
# include <asio/experimental/awaitable_operators.hpp>
#endif //RIWO_USING_BOOST_ASIO

using namespace asio::experimental::awaitable_operators;

namespace riwo::coro
{

template <typename Rep, typename Period, concepts::sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORO_TAPI auto sleep_for (
	concepts::sched auto &&exec, duration<Rep,Period> rtime, Token &&token = use_awaitable
);

template <typename Rep, typename Period, concepts::sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORO_TAPI auto sleep_for (
	duration<Rep,Period> rtime, Token &&token = use_awaitable
);

template <typename Clock, typename Duration, concepts::sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORO_TAPI auto sleep_until (
	concepts::sched auto &&exec, time_point<Clock,Duration> atime, Token &&token = use_awaitable
);

template <typename Clock, typename Duration, concepts::sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORO_TAPI auto sleep_until (
	time_point<Clock,Duration> atime, Token &&token = use_awaitable
);

template <typename T>
[[nodiscard]] RIWO_CORO_TAPI
awaitable<T> wait(const std::future<T> &future);

[[nodiscard]] RIWO_CORO_API
awaitable<void> wait(const asio::thread_pool &pool);

[[nodiscard]] RIWO_CORO_API
awaitable<void> wait (const std::thread &thread);

[[nodiscard]] RIWO_CORO_API
awaitable<void> wait(const jthread &thread);

template <concepts::sched Exec = io_executor_t>
[[nodiscard]] RIWO_CORO_TAPI
awaitable<asio::any_io_executor> goto_exec(Exec &&exec = get_executor());

[[nodiscard]] RIWO_CORO_API
awaitable<asio::any_io_executor> goto_thread();

template <concepts::any_async_tf_opt_token Token>
RIWO_CORO_TAPI bool check_error (
	Token &token, const error_code &error, const char *message = nullptr
) requires (not std::is_const_v<Token>);

namespace literals
{

[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_y  (unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_mon(unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_d  (unsigned long long value);

[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_h  (unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_min(unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_s  (unsigned long long value);

[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_ms (unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_us (unsigned long long value);
[[nodiscard]] RIWO_CORO_API awaitable<error_code> operator""_ns (unsigned long long value);

}} //namespace riwo::coro::literals
#include <riwo/coro/detail/utils.h>


#endif //RIWO_CORO_UTILS_H
