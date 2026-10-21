// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_CONFIG_H
#define RIWO_HTTP_EXP_CLIENT_CONFIG_H

#include <riwo/http_exp/protocol/policy.h>
#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/client/cookie_jar.h>

namespace riwo::http_exp
{

struct proxy_policy
{
	optional<riwo::url> forward_proxy {};
};

struct tls_policy
{
	bool verify_peer = true;
	optional<std::filesystem::path> certificate_authority_file {};
};

struct timeout_policy
{
	milliseconds connect {30000};
	milliseconds header {30000};
	milliseconds body_idle {30000};
	optional<milliseconds> overall {};
	milliseconds pool_wait {30000};
};

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

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
struct h2_config
{
	uint32_t initial_stream_window = 65'535;
	uint32_t initial_connection_window = 1024 * 1024;
	uint32_t max_concurrent_streams = 100;
	uint32_t max_frame_size = 16 * 1024;
	size_t max_header_list_size = 64 * 1024;
	size_t max_dynamic_table_size = 4 * 1024;
	size_t inbound_stream_buffer = 256 * 1024;
	size_t outbound_stream_buffer = 256 * 1024;
	size_t inbound_connection_buffer = 8 * 1024 * 1024;
	size_t outbound_connection_buffer = 8 * 1024 * 1024;
	bool enable_extended_connect = false;
};
#endif

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
	message_limits response_limits {};
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	h2_config h2 {};
#endif
	bool automatic_decompression = true;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_CONFIG_H
