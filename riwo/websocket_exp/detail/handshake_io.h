// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_HANDSHAKE_IO_H
#define RIWO_WEBSOCKET_EXP_DETAIL_HANDSHAKE_IO_H

#include <riwo/core/async_expected.h>
#include <riwo/websocket_exp/cxx/attributes.h>
#include <riwo/websocket_exp/cxx/concepts.h>
#include <riwo/websocket_exp/types.h>

namespace riwo::websocket_exp::detail
{

[[nodiscard]] constexpr std::chrono::nanoseconds
effective_handshake_timeout(std::chrono::milliseconds timeout) noexcept
{
	return timeout > std::chrono::milliseconds::zero() ?
		   std::chrono::duration_cast<std::chrono::nanoseconds>(timeout) :
		   std::chrono::nanoseconds(1);
}

template <typename Value, typename Exec>
[[nodiscard]] auto handshake_fallback(const Exec &exec,
	optional<stream_config> fallback_config)
{
	return [exec, fallback_config = std::move(fallback_config)]() mutable -> Value
	{
		if constexpr( std::constructible_from<Value,const Exec&,stream_config> )
			return Value(exec, fallback_config.value_or(stream_config{}));
		else
			return Value(exec);
	};
}

template <typename Value, typename Exec, typename Initiator, typename Handler>
void start_handshake_io(const Exec &exec, Initiator initiation,
	std::chrono::nanoseconds timeout, optional<stream_config> fallback_config, Handler &&handler)
{
	riwo::initiate_timed_io<Value>(exec, std::move(initiation), timeout,
		handshake_fallback<Value>(exec, std::move(fallback_config)),
		std::forward<Handler>(handler)
	);
}

template <typename Value, typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_WEBSOCKET_EXP_TAPI auto initiate_handshake_io(const Exec &exec, Initiator initiation,
	std::chrono::milliseconds timeout, optional<stream_config> fallback_config, Token &&token)
{
	const auto effective_timeout = effective_handshake_timeout(timeout);
	return riwo::initiate_timed_io<Value>(exec, std::move(initiation),
		effective_timeout,
		handshake_fallback<Value>(exec, std::move(fallback_config)),
		std::forward<Token>(token)
	);
}

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_HANDSHAKE_IO_H
