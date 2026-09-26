// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_TOKEN_CONCEPTS_H
#define RIWO_CORE_UTILS_TOKEN_CONCEPTS_H

#include <riwo/core/utils/opt_token.h>

namespace riwo
{

template <typename>
struct is_use_future : std::false_type {};

template <typename Allocator>
struct is_use_future<asio::use_future_t<Allocator>> : std::true_type {};

template <typename Allocator>
struct is_use_future<detail::std_error_token_t<asio::use_future_t<Allocator>>> : std::true_type {};

template <typename T>
constexpr bool is_use_future_v = is_use_future<T>::value;

template <typename>
struct is_detached : std::false_type {};

template <>
struct is_detached<detached_t> : std::true_type {};

template <typename T>
constexpr bool is_detached_v = is_detached<T>::value;

template <typename>
struct is_deferred : std::false_type {};

template <>
struct is_deferred<deferred_t> : std::true_type {};

template <typename T>
constexpr bool is_deferred_v = is_deferred<T>::value;

template <typename>
struct is_use_sync : std::false_type {};

template <>
struct is_use_sync<use_sync_t> : std::true_type {};

template <typename T>
constexpr bool is_use_sync_v = is_use_sync<T>::value;

template <typename>
struct is_use_awaitable : std::false_type {};

template <concepts::exec Exec>
struct is_use_awaitable<detail::std_error_token_t<asio::use_awaitable_t<Exec>>> : std::true_type {};

template <concepts::exec Exec>
struct is_use_awaitable<asio::use_awaitable_t<Exec>> : std::true_type {};

template <typename T>
constexpr bool is_use_awaitable_v = is_use_awaitable<T>::value;

template <typename>
struct is_redirect_error : std::false_type {};

template <typename Token>
struct is_redirect_error<redirect_error_t<Token>> : std::true_type {};

template <typename T>
constexpr bool is_redirect_error_v = is_redirect_error<T>::value;

template <typename>
struct is_cancellation_slot_binder : std::false_type {};

template <typename Token, typename CancellationSlot>
struct is_cancellation_slot_binder<cancellation_slot_binder<Token,CancellationSlot>> : std::true_type {};

template <typename T>
constexpr bool is_cancellation_slot_binder_v = is_cancellation_slot_binder<T>::value;

template <typename Token, typename...Args>
struct is_async_opt_token {
	static constexpr bool value = asio::completion_token_for<Token,void(Args...)>;
};

template <typename Token, typename...Args>
constexpr bool is_async_opt_token_v = is_async_opt_token<Token,Args...>::value;

template <typename Token>
struct is_any_async_opt_token
{
private: // Fucking msvc !!!
	using token_t = std::remove_cvref_t<Token>;

	template <size_t...I>
	[[nodiscard]] static consteval bool helper(std::index_sequence<I...>)
	{
		return is_async_opt_token_v <
			Token, std::tuple_element_t <
				I, typename function_traits<token_t>::arg_types
			>...
		>;
	}

	// Fucking msvc !!!
	[[nodiscard]] static consteval bool helper()
	{
		if constexpr( not is_function_v<token_t> or
					  is_use_future_v<token_t> or is_use_awaitable_v<token_t> or
					  is_deferred_v<token_t> or is_cancellation_slot_binder_v<token_t> )
			return is_async_opt_token_v<Token>;

		else if constexpr( is_void_func_v<token_t> )
		{
			if constexpr( constexpr auto arg_count = function_traits<token_t>::arg_count; arg_count == 0 )
				return is_async_opt_token_v<Token>;
			else
				return helper(std::make_index_sequence<arg_count>{});
		}
		else
			return false;
	}

public:
	static constexpr bool value = helper();
};

template <typename Token>
constexpr bool is_any_async_opt_token_v = is_any_async_opt_token<Token>::value;

template <typename>
struct is_redirect_time : std::false_type {};

template <typename Token>
struct is_redirect_time<redirect_time_t<Token>> : is_any_async_opt_token<Token> {};

template <typename T>
constexpr bool is_redirect_time_v = is_redirect_time<T>::value;

template <typename Token, typename...Args>
struct is_async_tf_opt_token
{
	static constexpr bool value = []() consteval -> bool
	{
		if constexpr( is_async_opt_token_v<Token,Args...> )
			return true;
		else
		{
			using token_t = std::remove_cvref_t<Token>;
			if constexpr( is_redirect_time_v<token_t> )
				return is_async_opt_token_v<typename token_t::token_t, Args...>;
			else
				return false;
		}
	}();
};

template <typename Token, typename...Args>
constexpr bool is_async_tf_opt_token_v = is_async_tf_opt_token<Token,Args...>::value;

template <typename Token>
struct is_any_async_tf_opt_token
{
	static constexpr bool value = []() consteval -> bool
	{
		if constexpr( is_any_async_opt_token_v<Token> )
			return true;
		else
		{
			using token_t = std::remove_cvref_t<Token>;
			if constexpr( is_redirect_time_v<token_t> )
				return is_any_async_opt_token_v<typename token_t::token_t>;
			else
				return false;
		}
	}();
};

template <typename Token>
constexpr bool is_any_async_tf_opt_token_v = is_any_async_tf_opt_token<Token>::value;

template <typename Token>
struct is_error_code_token
{
	static constexpr bool value =
		std::is_same_v<Token,error_code&> or
		std::is_same_v<Token,std::error_code&>;
};

template <typename Token>
constexpr bool is_error_code_token_v = is_error_code_token<Token>::value;

template <typename Token>
struct is_sync_opt_token
{
	static constexpr bool value =
		is_error_code_token_v<Token> or
		std::is_same_v<std::remove_cvref_t<Token>,use_sync_t>;
};

template <typename Token>
constexpr bool is_sync_opt_token_v = is_sync_opt_token<Token>::value;

template <typename Token, typename...Args>
struct is_opt_token
{
	static constexpr bool value =
		is_async_opt_token_v<Token,Args...> or
		is_sync_opt_token_v<Token>;
};

template <typename Token, typename...Args>
constexpr bool is_opt_token_v = is_opt_token<Token,Args...>::value;

template <typename Token>
struct is_any_opt_token : std::disjunction<is_async_opt_token<Token>, is_sync_opt_token<Token>> {};

template <typename Token>
constexpr bool is_any_opt_token_v = is_any_opt_token<Token>::value;

template <typename Token, typename...Args>
struct is_tf_opt_token
{
	static constexpr bool value =
		is_async_tf_opt_token_v<Token,Args...> or
		is_sync_opt_token_v<Token>;
};

template <typename Token, typename...Args>
constexpr bool is_tf_opt_token_v = is_tf_opt_token<Token,Args...>::value;

template <typename Token>
struct is_any_tf_opt_token : std::disjunction<is_any_async_tf_opt_token<Token>, is_sync_opt_token<Token>> {};

template <typename Token>
constexpr bool is_any_tf_opt_token_v = is_any_tf_opt_token<Token>::value;

template <typename Token, typename...Args>
struct is_dis_sync_tf_opt_token
{
	static constexpr bool value =
		is_tf_opt_token_v<Token,Args...> and
		not is_sync_opt_token_v<Token>;
};

template <typename Token, typename...Args>
constexpr bool is_dis_sync_tf_opt_token_v = is_dis_sync_tf_opt_token<Token,Args...>::value;

namespace concepts
{

template <typename T>
concept use_awaitable = is_use_awaitable_v<T>;

template <typename T>
concept use_awaitable_p = use_awaitable<std::remove_cvref_t<T>>;

template <typename T>
concept use_future = is_use_future_v<T>;

template <typename T>
concept use_future_p = use_future<std::remove_cvref_t<T>>;

template <typename T>
concept detached = is_detached_v<T>;

template <typename T>
concept detached_p = detached<std::remove_cvref_t<T>>;

template <typename T>
concept redirect_error = is_redirect_error_v<T>;

template <typename T>
concept redirect_error_p = redirect_error<std::remove_cvref_t<T>>;

template <typename T>
concept cancellation_slot_binder = is_cancellation_slot_binder_v<T>;

template <typename T>
concept cancellation_slot_binder_p = cancellation_slot_binder<std::remove_cvref_t<T>>;

template <typename T>
concept redirect_time = is_redirect_time_v<T>;

template <typename T>
concept redirect_time_p = redirect_time<std::remove_cvref_t<T>>;

template <typename Token, typename...Args>
concept async_opt_token = is_async_opt_token_v<Token,Args...>;

template <typename Token>
concept any_async_opt_token = is_any_async_opt_token_v<Token>;

template <typename Token, typename...Args>
concept async_tf_opt_token = is_async_tf_opt_token_v<Token,Args...>;

template <typename Token>
concept any_async_tf_opt_token = is_any_async_tf_opt_token_v<Token>;

template <typename Token>
concept error_code_token = is_error_code_token_v<Token>;

template <typename Token>
concept sync_opt_token = is_sync_opt_token_v<Token>;

template <typename Token, typename...Args>
concept opt_token = is_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept tf_opt_token = is_tf_opt_token_v<Token,Args...>;

template <typename Token>
concept any_opt_token = is_any_opt_token_v<Token>;

template <typename Token>
concept any_tf_opt_token = is_any_tf_opt_token_v<Token>;

template <typename Token, typename...Args>
concept dis_sync_tf_opt_token = is_dis_sync_tf_opt_token_v<Token,Args...>;

}} //namespace riwo::concepts


#endif //RIWO_CORE_UTILS_TOKEN_CONCEPTS_H
