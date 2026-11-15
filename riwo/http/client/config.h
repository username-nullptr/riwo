// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_CONFIG_H
#define RIWO_HTTP_CLIENT_CONFIG_H

#include <riwo/http/protocol/policy.h>
#include <riwo/http/protocol/limits.h>
#include <riwo/http/protocol/v2/config.h>

#include <riwo/http/transport/connect_options.h>
#include <riwo/http/client/cookie_jar.h>
#include <riwo/http/timeout.h>

namespace riwo::http
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

#if RIWO_HTTP_HTTP2_SUPPORT
	v2_config h2 {};
#endif
	bool auto_decompression = true;
};

} //namespace riwo::http


#endif //RIWO_HTTP_CLIENT_CONFIG_H
