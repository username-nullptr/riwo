// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_CONFIG_H
#define RIWO_EXP_HTTP_CLIENT_CONFIG_H

#include <riwo/experimental/http/protocol/policy.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/protocol/v2/config.h>

#include <riwo/experimental/http/transport/connect_options.h>
#include <riwo/experimental/http/client/cookie_jar.h>
#include <riwo/experimental/http/timeout.h>

namespace riwo::experimental::http
{

struct pool_config
{
	size_t max_sessions_per_origin = 4;
	size_t max_waiters_per_origin = 1024;
	milliseconds idle_timeout {60000};
};

struct redirect_policy
{
	size_t max_redirects = 5;
	bool allow_https_to_http = false;
};

struct retry_policy
{
	size_t max_attempts = 2;
	bool retry_idempotent = true;
	bool retry_non_idempotent = false;
};

struct client_config
{
	protocol_policy protocols {};

	proxy_policy proxy {};
	tls_policy tls {};

	timeout_policy timeout {};
	pool_config pool {};

	redirect_policy redirects {};
	retry_policy retries {};

	cookie_config cookies {};
	message_limits reply_limits {};

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2_config h2 {};
#endif
	bool auto_decompression = true;
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_CLIENT_CONFIG_H
