// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_DETAIL_UTILS_H
#define RIWO_CORO_DETAIL_UTILS_H

#include <riwo/core/execution.h>
#include <limits>
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

namespace literals { namespace detail
{

struct duration_literal_value
{
	unsigned long long value = 0;
	bool valid = true;
	bool overflow = false;
};

template <char... Digits>
consteval duration_literal_value parse_duration_literal()
{
	constexpr char digits[] = {Digits...};
	duration_literal_value result;

	size_t index = 0;
	unsigned int base = 10;

	if constexpr( sizeof...(Digits) > 1 )
	{
		if( digits[0] == '0' )
		{
			if( digits[1] == 'x' or digits[1] == 'X' )
			{
				base = 16;
				index = 2;
			}
			else if( digits[1] == 'b' or digits[1] == 'B' )
			{
				base = 2;
				index = 2;
			}
			else
			{
				base = 8;
				index = 1;
			}
		}
	}
	for(; index < sizeof...(Digits); ++index)
	{
		const char character = digits[index];
		if( character == '\'' )
			continue;

		unsigned int digit = 0;
		if( character >= '0' and character <= '9' )
			digit = static_cast<unsigned int>(character - '0');

		else if( character >= 'a' and character <= 'f' )
			digit = static_cast<unsigned int>(character - 'a' + 10);

		else if( character >= 'A' and character <= 'F' )
			digit = static_cast<unsigned int>(character - 'A' + 10);
		else
		{
			result.valid = false;
			continue;
		}

		if( digit >= base )
		{
			result.valid = false;
			continue;
		}
		constexpr auto maximum = (std::numeric_limits<unsigned long long>::max)();
		if( result.value > (maximum - digit) / base )
			result.overflow = true;

		else if( not result.overflow )
			result.value = result.value * base + digit;
	}
	return result;
}

template <typename Duration, char... Digits>
consteval Duration checked_duration_literal()
{
	constexpr auto value = parse_duration_literal<Digits...>();

	static_assert(value.valid, "Invalid duration literal.");
	static_assert(not value.overflow, "Duration literal is too large.");

	using rep_t = Duration::rep;
	static_assert (
		value.value <= static_cast<unsigned long long>((std::numeric_limits<rep_t>::max)()),
		"Duration literal cannot be represented by its duration type."
	);
	return Duration(static_cast<rep_t>(value.value));
}

} //namespace detail

template <char... Digits>
awaitable<error_code> operator""_y()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::years, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_mon()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::months, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_d()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::days, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_h()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::hours, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_min()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::minutes, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_s()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::seconds, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_ms()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::milliseconds, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_us()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::microseconds, Digits...>());
}

template <char... Digits>
awaitable<error_code> operator""_ns()
{
	return sleep_for(detail::checked_duration_literal<std::chrono::nanoseconds, Digits...>());
}

} } //namespace riwo::coro::literals


#endif //RIWO_CORO_DETAIL_UTILS_H
