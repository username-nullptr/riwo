// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_TOKEN_CONCEPTS_H
#define RIWO_CORE_OPERATION_TOKEN_CONCEPTS_H

#include <riwo/core/operation/token_traits.h>

namespace riwo::concepts
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

template <typename T>
concept as_tuple = is_as_tuple_v<T>;

template <typename T>
concept as_tuple_p = as_tuple<std::remove_cvref_t<T>>;

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

template <typename Token, typename...Args>
concept dis_func_opt_token = is_dis_func_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_func_tf_opt_token = is_dis_func_tf_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_detached_opt_token = is_dis_detached_opt_token_v<Token,Args...>;

template <typename Token, typename...Args>
concept dis_detached_tf_opt_token = is_dis_detached_tf_opt_token_v<Token,Args...>;

} //namespace riwo::concepts


#endif //RIWO_CORE_OPERATION_TOKEN_CONCEPTS_H
