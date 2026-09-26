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

awaitable<error_code> operator""_y(unsigned long long value)
{
	using rep_t = std::chrono::years::rep;
	return sleep_for(std::chrono::years(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_mon(unsigned long long value)
{
	using rep_t = std::chrono::months::rep;
	return sleep_for(std::chrono::months(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_d(unsigned long long value)
{
	using rep_t = std::chrono::days::rep;
	return sleep_for(std::chrono::days(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_h(unsigned long long value)
{
	using rep_t = std::chrono::hours::rep;
	return sleep_for(std::chrono::hours(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_min(unsigned long long value)
{
	using rep_t = std::chrono::minutes::rep;
	return sleep_for(std::chrono::seconds(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_s(unsigned long long value)
{
	using rep_t = std::chrono::seconds::rep;
	return sleep_for(std::chrono::seconds(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_ms(unsigned long long value)
{
	using rep_t = std::chrono::milliseconds::rep;
	return sleep_for(std::chrono::milliseconds(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_us(unsigned long long value)
{
	using rep_t = std::chrono::microseconds::rep;
	return sleep_for(std::chrono::microseconds(static_cast<rep_t>(value)));
}

awaitable<error_code> operator""_ns(unsigned long long value)
{
	using rep_t = std::chrono::nanoseconds::rep;
	return sleep_for(std::chrono::nanoseconds(static_cast<rep_t>(value)));
}

}} //namespace riwo::coro::literals
