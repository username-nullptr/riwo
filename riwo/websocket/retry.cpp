// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "retry.h"
#include <cmath>

namespace riwo::websocket
{

retry_open_decision retry_open_decision::stop() noexcept
{
	return {};
}

retry_open_decision retry_open_decision::retry_after(std::chrono::milliseconds delay) noexcept
{
	return {
		.retry = true,
		.delay = std::max(delay,std::chrono::milliseconds::zero())
	};
}

std::chrono::milliseconds suggest_retry_open_delay
(const retry_open_options &options, size_t failed_attempts) noexcept
{
	if( failed_attempts == 0 )
		return std::chrono::milliseconds::zero();

	const auto initial_count = std::max<int64_t>(0, options.initial_delay.count());
	const auto maximum_count = std::max<int64_t>(initial_count, options.max_delay.count());

	const auto multiplier = options.multiplier >= 1.0 and
		options.multiplier <= std::numeric_limits<double>::max() ?
		options.multiplier : 1.0;

	const auto jitter = options.jitter >= 0.0 and options.jitter <= 1.0 ?
		options.jitter : 0.0;

	const auto exponent = static_cast<long double>(failed_attempts - 1);
	const auto factor = std::pow(static_cast<long double>(multiplier), exponent);

	const auto uncapped = static_cast<long double>(initial_count) * factor;
	const auto cap = static_cast<long double>(maximum_count);
	auto value = std::min(uncapped, cap);

	if( jitter > 0.0 and value > 0.0L )
	{
		static std::atomic_uint64_t sequence { 0x9E3779B97F4A7C15ULL };
		auto random = sequence.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed);

		random ^= random >> 12;
		random ^= random << 25;
		random ^= random >> 27;

		const auto unit = static_cast<long double>(random >> 11) /
			static_cast<long double>(uint64_t{1} << 53);

		const auto offset = (unit * 2.0L - 1.0L) *
			static_cast<long double>(jitter);

		value *= 1.0L + offset;
	}
	value = std::clamp(value, 0.0L, cap);

	return std::chrono::milliseconds (
		static_cast<std::chrono::milliseconds::rep>(value)
	);
}

retry_open_decision default_retry_open_decision
(const retry_open_context &context, std::chrono::milliseconds suggested_delay) noexcept
{
	if( not context.error or context.error == asio::error::operation_aborted )
		return retry_open_decision::stop();

	if( context.http_status )
	{
		const auto status = static_cast<uint32_t>(*context.http_status);
		if( status == 408 or status == 429 or status >= 500 )
			return retry_open_decision::retry_after(suggested_delay);

		return retry_open_decision::stop();
	}
	if( context.error.category() == protocol_error_category() or
		context.error.category() == error_category() )
		return retry_open_decision::stop();

	if( context.error == asio::error::eof or
		context.error == asio::error::broken_pipe or
		context.error == asio::error::connection_aborted or
		context.error == asio::error::connection_refused or
		context.error == asio::error::connection_reset or
		context.error == asio::error::host_unreachable or
		context.error == asio::error::network_down or
		context.error == asio::error::network_reset or
		context.error == asio::error::network_unreachable or
		context.error == asio::error::not_connected or
		context.error == asio::error::timed_out or
		context.error == asio::error::try_again or
		context.error == asio::error::host_not_found_try_again )
		return retry_open_decision::retry_after(suggested_delay);

	// Unknown categories include TLS verification failures. Repeating those
	// indefinitely is less safe than requiring an explicit policy override.
	return retry_open_decision::stop();
}

} //namespace riwo::websocket
