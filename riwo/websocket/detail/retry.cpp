// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/retry.h>

namespace riwo::websocket::detail
{

bool valid_retry_open_options(const retry_open_options &options) noexcept
{
	return options.initial_delay >= std::chrono::milliseconds::zero() and
		   options.max_delay >= options.initial_delay and
		   options.multiplier >= 1.0 and
		   options.multiplier <= std::numeric_limits<double>::max() and
		   options.jitter >= 0.0 and options.jitter <= 1.0;
}

void observe_retry_open
(const retry_open_options &options, retry_open_event event, const retry_open_context &context) noexcept
{
	if( not options.observe )
		return ;
	try {
		options.observe(event, context);
	}
	catch(...) {}
}

retry_open_decision decide_retry_open
(const retry_open_options &options, const retry_open_context &context) noexcept
{
	const auto suggested = suggest_retry_open_delay(options, context.attempt);
	try {
		if( options.decide )
			return options.decide(context, suggested);
	}
	catch(...) {
		return retry_open_decision::stop();
	}
	return default_retry_open_decision(context, suggested);
}

} //namespace riwo::websocket::detail
