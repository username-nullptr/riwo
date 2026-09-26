// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_UTILS_H
#define RIWO_CORO_DETAIL_UTILS_H

#include <riwo/core/execution.h>
#include <thread>

namespace riwo::coro
{

template <typename Rep, typename Period, concepts::sleep_opt_token Token>
auto sleep_for(concepts::sched auto &&exec, duration<Rep,Period> rtime, Token &&token)
{
	return riwo::sleep_for(std::forward<decltype(exec)>(exec), rtime, std::forward<Token>(token));
}

template <typename Rep, typename Period, concepts::sleep_opt_token Token>
auto sleep_for(duration<Rep,Period> rtime, Token &&token)
{
	return riwo::sleep_for(rtime, std::forward<Token>(token));
}

template <typename Rep, typename Period, concepts::sleep_opt_token Token>
auto sleep_until(concepts::sched auto &&exec, time_point<Rep,Period> atime, Token &&token)
{
	return riwo::sleep_until(std::forward<decltype(exec)>(exec), atime, std::forward<Token>(token));
}

template <typename Rep, typename Period, concepts::sleep_opt_token Token>
auto sleep_until(time_point<Rep,Period> atime, Token &&token)
{
	return riwo::sleep_until(atime, std::forward<Token>(token));
}

template <typename T>
awaitable<T> wait(const std::future<T> &future)
{
	auto exec = co_await asio::this_coro::executor;
	co_return co_await dispatch(exec, [&future]() mutable -> awaitable<T>
	{
		auto res = co_await local_dispatch([&future] {
			return remove_const(future).get();
		}, use_awaitable);

		if constexpr( std::is_void_v<T> )
			co_return ;
		else
			co_return res.first;
	},
	use_awaitable);
}

template <concepts::sched Exec>
awaitable<asio::any_io_executor> goto_exec(Exec &&exec)
{
	auto current_exec = co_await asio::this_coro::executor;
	co_return co_await asio::async_initiate<decltype(asio::use_awaitable), void(asio::any_io_executor)>
	([previous_exec = std::move(current_exec), target_exec = get_executor_helper(exec)](auto completion_handler)
	{
		auto work_guard = asio::make_work_guard(completion_handler);
		asio::post(target_exec, [
			posted_handler = std::move(completion_handler), guard = std::move(work_guard),
			return_exec = std::move(previous_exec)
		]() mutable
		{
			RIWO_UNUSED(guard);
			std::move(posted_handler)(std::move(return_exec));
		});
	},
	asio::use_awaitable);
}

template <concepts::any_async_tf_opt_token Token>
bool check_error(Token &token, const error_code &error, const char *message)
	requires (not std::is_const_v<Token>)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_use_awaitable_v<token_t> )
	{
		if( error )
		{
			system_error::loc_throw(error,
				with_location(message ? message : "")
			);
		}
		return true;
	}
	else if constexpr( is_redirect_error_v<token_t> )
	{
		token.ec_ = error;
		return false;
	}
	else if constexpr( is_cancellation_slot_binder_v<token_t> )
		return check_error(token.get(), error, message);

	else if constexpr( is_redirect_time_v<token_t> )
		return check_error(token.token, error, message);
	else
	{
		static_assert(false, "Unsupported token type.");
		return false;
	}
}

} //namespace riwo::coro


#endif //RIWO_CORO_DETAIL_UTILS_H
