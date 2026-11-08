// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_TOKENS_H
#define RIWO_CORE_OPERATION_TOKENS_H

#include <riwo/core/operation/detail/error_token.h>

#if RIWO_USING_BOOST_ASIO
# include <boost/asio/experimental/awaitable_operators.hpp>
#else //RIWO_USING_BOOST_ASIO
# include <asio/experimental/awaitable_operators.hpp>
#endif //RIWO_USING_BOOST_ASIO

namespace riwo
{

template <concepts::exec Exec = asio::any_io_executor>
using use_basic_awaitable_t = detail::std_error_token_t <
	asio::use_awaitable_t<Exec>
>;

using use_awaitable_t = use_basic_awaitable_t<asio::any_io_executor>;
constexpr use_awaitable_t use_awaitable(asio::use_awaitable);

template <typename Allocator = std::allocator<void>>
using use_basic_future_t = detail::std_error_token_t <
	asio::use_future_t<Allocator>
>;

using use_future_t = use_basic_future_t<std::allocator<void>>;
constexpr use_future_t use_future(asio::use_future);

using detached_t = asio::detached_t;
constexpr auto detached = asio::detached;

using deferred_t = asio::deferred_t;
constexpr auto deferred = asio::deferred;

struct use_sync_t {};
constexpr use_sync_t use_sync;

template <typename CompletionToken>
using as_tuple_t = asio::as_tuple_t<CompletionToken>;

using partial_as_tuple = asio::partial_as_tuple;
constexpr auto as_tuple = asio::as_tuple;

template <typename Token>
using redirect_error_t = asio::redirect_error_t<Token>;

template <typename Token, typename CancellationSlot>
using cancellation_slot_binder = asio::cancellation_slot_binder<Token, CancellationSlot>;

} //namespace riwo


#endif //RIWO_CORE_OPERATION_TOKENS_H
