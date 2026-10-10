// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "utils.h"
#include <thread>

namespace riwo::coro { namespace
{

template <typename Thread>
awaitable<void> wait_thread(const Thread &thread)
{
	auto exec = co_await asio::this_coro::executor;
	co_return co_await dispatch(exec, [&thread]() mutable -> awaitable<void>
	{
		co_await local_dispatch([&thread] {
			return remove_const(thread).join();
		}, use_awaitable);
		co_return ;
	},
	use_awaitable);
}

} //namespace

awaitable<void> wait(const asio::thread_pool &pool)
{
	auto exec = co_await asio::this_coro::executor;
	co_return co_await dispatch(exec, [&pool]() mutable -> awaitable<void>
	{
		co_await local_dispatch([&pool] {
			return remove_const(pool).wait();
		}, use_awaitable);
		co_return ;
	},
	use_awaitable);
}

awaitable<void> wait(const std::thread &thread)
{
	return wait_thread(thread);
}

awaitable<void> wait(const jthread &thread)
{
	return wait_thread(thread);
}

awaitable<asio::any_io_executor> goto_thread()
{
	auto current_exec = co_await asio::this_coro::executor;
	co_return co_await asio::async_initiate<decltype(asio::use_awaitable),void(asio::any_io_executor)>
	([previous_exec = std::move(current_exec)](auto completion_handler)
	{
		auto work_guard = asio::make_work_guard(completion_handler);
		std::thread([
			thread_handler = std::move(completion_handler),
			guard = std::move(work_guard), previous_exec
		]() mutable
		{
			RIWO_UNUSED(guard);
			std::move(thread_handler)(std::move(previous_exec));
		})
		.detach();
	},
	asio::use_awaitable);
}

namespace literals
{

namespace
{

template <typename Duration>
awaitable<error_code> sleep_literal(long double value)
{
	using duration_t = std::chrono::duration<long double, typename Duration::period>;
	return sleep_for(duration_t(value));
}

} //namespace

awaitable<error_code> operator""_y(long double value)
{
	return sleep_literal<std::chrono::years>(value);
}

awaitable<error_code> operator""_mon(long double value)
{
	return sleep_literal<std::chrono::months>(value);
}

awaitable<error_code> operator""_d(long double value)
{
	return sleep_literal<std::chrono::days>(value);
}

awaitable<error_code> operator""_h(long double value)
{
	return sleep_literal<std::chrono::hours>(value);
}

awaitable<error_code> operator""_min(long double value)
{
	return sleep_literal<std::chrono::minutes>(value);
}

awaitable<error_code> operator""_s(long double value)
{
	return sleep_literal<std::chrono::seconds>(value);
}

awaitable<error_code> operator""_ms(long double value)
{
	return sleep_literal<std::chrono::milliseconds>(value);
}

awaitable<error_code> operator""_us(long double value)
{
	return sleep_literal<std::chrono::microseconds>(value);
}

awaitable<error_code> operator""_ns(long double value)
{
	return sleep_literal<std::chrono::nanoseconds>(value);
}

}} //namespace riwo::coro::literals
