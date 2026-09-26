// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_GLOBAL_H
#define RIWO_CORE_DETAIL_GLOBAL_H

namespace riwo
{

template<typename Rep, typename Period>
decltype(auto) get_associated_redirect_time
(concepts::any_async_tf_opt_token auto &&token, const duration<Rep,Period> &def_time)
{
	using Token = decltype(token);
	using token_t = std::remove_cvref_t<Token>;

	if constexpr( is_redirect_time_v<token_t> )
		return return_reference(token.time);
	else
		return std::chrono::duration_cast<milliseconds>(def_time);
}

decltype(auto) get_associated_redirect_time(concepts::any_async_tf_opt_token auto &&token)
{
	using token_t = decltype(token);
	return get_associated_redirect_time(std::forward<token_t>(token), milliseconds(0));
}

constexpr decltype(auto) unbound_redirect_time(concepts::any_async_tf_opt_token auto &&token)
{
	using Token = decltype(token);
	using token_t = std::remove_cvref_t<Token>;

	if constexpr( is_redirect_time_v<token_t> )
		return return_reference(std::forward<Token>(token).token);
	else
		return std::forward<Token>(token);
}

namespace operators
{

template <concepts::any_async_tf_opt_token Token>
auto operator|(Token &&token, error_code &error)
	requires (not is_redirect_error_v<std::remove_cvref_t<Token>>)
{
	if constexpr( is_redirect_time_v<Token> )
	{
		auto _token = asio::redirect_error(token.token, error);
		using token_t = std::remove_cvref_t<decltype(_token)>;
		return redirect_time_t<token_t>(std::move(_token), token.time);
	}
	else
		return asio::redirect_error(std::forward<Token>(token), error);
}

template <concepts::any_async_tf_opt_token Token>
auto operator|(Token &&token, const asio::cancellation_slot &slot)
	requires (not is_cancellation_slot_binder_v<std::remove_cvref_t<Token>>)
{
	if constexpr( is_redirect_time_v<Token> )
	{
		auto _token = asio::bind_cancellation_slot(slot, token.token);
		using token_t = std::remove_cvref_t<decltype(_token)>;
		return redirect_time_t<token_t>(std::move(_token), token.time);
	}
	else
		return asio::bind_cancellation_slot(slot, std::forward<Token>(token));
}

template <typename Rep, typename Period>
auto operator|(concepts::any_async_opt_token auto &&token, const duration<Rep,Period> &d)
{
	using token_t = decltype(token);
	return redirect_time(std::forward<token_t>(token), d);
}

template <typename Clock, typename Duration>
auto operator|(concepts::any_async_opt_token auto &&token, const time_point<Clock,Duration> &tp)
{
	using token_t = decltype(token);
	return redirect_time(std::forward<token_t>(token), tp);
}

}} //namespace riwo::operators


#endif //RIWO_CORE_DETAIL_GLOBAL_H
