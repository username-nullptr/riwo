// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_CLIENT_H
#define RIWO_WEBSOCKET_CLIENT_H

#include <riwo/websocket/stream.h>
#include <riwo/http/client.h>

namespace riwo::websocket
{

enum class proxy_type : uint8_t {
	http, socks5,
};

struct RIWO_WEBSOCKET_API proxy_config
{
	proxy_type type = proxy_type::http;
	url endpoint {};

	optional<std::string> authorization {};
	optional<std::string> username {};
	optional<std::string> password {};

	proxy_config &set_basic_auth(std::string_view user, std::string_view secret);
	proxy_config &set_bearer_auth(std::string_view token);
};

using http::use_global_proxy_t;
using http::use_global_proxy;

using http::no_proxy_t;
using http::no_proxy;

using proxy_t = std::variant <
	proxy_config, use_global_proxy_t, no_proxy_t
>;

struct client_config
{
	stream_config stream {};
	std::chrono::milliseconds handshake_timeout {30000};

	proxy_t default_proxy = use_global_proxy;
	optional<bool> no_delay {true};
};

struct connect_request
{
	url endpoint {};
	http::request_arg request_options {};

	std::optional<proxy_t> proxy {};
	optional<stream_config> stream_options {};

	optional<std::chrono::milliseconds> handshake_timeout {};
	std::vector<std::string> subprotocols {};
	std::vector<extension> extensions {};

	size_t max_redirects = 0;
	bool allow_insecure_redirects = false;
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct basic_open_diagnostics
{
	using executor_type = Exec;
	using executor_t = executor_type;

	using reply_t = http::basic_reply<executor_t>;
	using reply_ptr = std::shared_ptr<reply_t>;

	url endpoint {};
	reply_ptr reply {};
};

using open_diagnostics = basic_open_diagnostics<>;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_WEBSOCKET_TAPI basic_client
{
	RIWO_DISABLE_COPY(basic_client)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using stream_t = basic_stream<executor_t>;

	using config_t = client_config;
	using connect_request_t = connect_request;

	using http_client_t = http::basic_client<executor_t>;
	using diagnostics_t = basic_open_diagnostics<executor_t>;

public:
	template <typename Token>
	static constexpr bool open_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,stream_t>;

public:
	basic_client() requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	explicit basic_client(config_t config) requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_client(Exec0 &&exec, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_t>
	);
	explicit basic_client(http_client_t &&http_client, config_t config = {});

	basic_client(basic_client &&other) noexcept;
	basic_client &operator=(basic_client &&other) noexcept;
	~basic_client();

public:
	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(connect_request_t request, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open (
		connect_request_t request, diagnostics_t &diagnostics,
		Token &&token = {}
	)
	requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(url endpoint, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open (
		url endpoint, diagnostics_t &diagnostics,
		Token &&token = {}
	)
	requires open_token_v<Token>;

public:
	[[nodiscard]] std::shared_ptr<http::cookie_jar> cookie_store() noexcept;
	[[nodiscard]] size_t pending_open_count() const noexcept;
	[[nodiscard]] config_t config() const;

	[[nodiscard]] const http_client_t &http_client() const noexcept;
	[[nodiscard]] http_client_t &http_client() noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;
	basic_client &cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using client = basic_client<>;

template <core_concepts::exec Exec, http::version_enum Version, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto open (
	http::basic_client<Exec,Version> &http_client, connect_request request, Token &&token = {}
) requires (Version == http::version::v11) and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>;

template <core_concepts::exec Exec, http::version_enum Version, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto open(http::basic_client<Exec,Version> &http_client,
	connect_request request, basic_open_diagnostics<Exec> &diagnostics, Token &&token = {}
) requires (Version == http::version::v11) and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>;

template <core_concepts::exec Exec, http::version_enum Version, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto open (
	http::basic_client<Exec,Version> &http_client, url endpoint, Token &&token = {}
) requires (Version == http::version::v11) and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>;

template <core_concepts::exec Exec, http::version_enum Version, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto open(http::basic_client<Exec,Version> &http_client,
	url endpoint, basic_open_diagnostics<Exec> &diagnostics, Token &&token = {}
) requires (Version == http::version::v11) and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>;

} //namespace riwo::websocket
#include <riwo/websocket/detail/client.h>


#endif //RIWO_WEBSOCKET_CLIENT_H
