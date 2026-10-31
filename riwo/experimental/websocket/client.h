// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_WEBSOCKET_CLIENT_H
#define RIWO_EXP_WEBSOCKET_CLIENT_H

#include <riwo/experimental/websocket/stream.h>
#include <riwo/experimental/http/client.h>

namespace riwo::experimental::websocket
{

struct client_config
{
	stream_config stream {};
	experimental::http::client_config http {};
	std::chrono::milliseconds handshake_timeout {30000};
};

struct RIWO_EXP_WEBSOCKET_API connect_request
{
	url endpoint {};
	experimental::http::fields headers {};

	optional<stream_config> stream_options {};
	optional<std::chrono::milliseconds> handshake_timeout {};

	std::vector<std::string> subprotocols {};
	std::vector<extension> extensions {};

	size_t max_redirects = 0;
	bool allow_insecure_redirects = false;

	connect_request() = default;
	explicit connect_request(url endpoint_value);
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct basic_open_diagnostics
{
	using executor_type = Exec;
	using executor_t = executor_type;

	url endpoint {};
	optional<experimental::http::response_head> response {};
	optional<experimental::http::protocol> selected_protocol {};
};

using open_diagnostics = basic_open_diagnostics<>;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_WEBSOCKET_TAPI basic_client
{
	RIWO_DISABLE_COPY(basic_client)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using config_t = client_config;

	using stream_t = basic_stream<executor_type>;
	using connect_request_t = connect_request;

	using http_client_t = experimental::http::basic_client<executor_type>;
	using diagnostics_t = basic_open_diagnostics<executor_type>;

	template <typename Token>
	static constexpr bool open_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,stream_t>;

public:
	basic_client() requires
		core_concepts::match_sched<io_executor_t,executor_type>;

	explicit basic_client(config_t config) requires
		core_concepts::match_sched<io_executor_t,executor_type>;

	template <typename Exec0>
	explicit basic_client(Exec0 &&executor, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_type>
	);
	explicit basic_client(http_client_t &&http_client, config_t config = {});
	~basic_client();

	basic_client(basic_client &&) noexcept;
	basic_client &operator=(basic_client &&) noexcept;

public:
	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(connect_request_t request, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(connect_request_t request, diagnostics_t &diagnostics, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(url endpoint, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(url endpoint, diagnostics_t &diagnostics, Token &&token = {})
		requires open_token_v<Token>;

public:
	[[nodiscard]] size_t pending_open_count() const noexcept;
	[[nodiscard]] config_t config() const;

	[[nodiscard]] experimental::http::cookie_jar_ptr cookie_store() const noexcept;
	[[nodiscard]] const http_client_t &http_client() const noexcept;
	[[nodiscard]] http_client_t &http_client() noexcept;

	[[nodiscard]] executor_type get_executor() const noexcept;
	basic_client &cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using client = basic_client<>;

template <core_concepts::exec Exec,
	core_concepts::dis_detached_tf_opt_token<error_code,basic_stream<Exec>> Token = use_sync_t>
[[nodiscard]] RIWO_EXP_WEBSOCKET_TAPI auto open (
	experimental::http::basic_client<Exec> &http_client, connect_request request, Token &&token = {}
);

template <core_concepts::exec Exec,
	core_concepts::dis_detached_tf_opt_token<error_code,basic_stream<Exec>> Token = use_sync_t>
[[nodiscard]] RIWO_EXP_WEBSOCKET_TAPI auto open (
	experimental::http::basic_client<Exec> &http_client, connect_request request,
	basic_open_diagnostics<Exec> &diagnostics, Token &&token = {}
);

template <core_concepts::exec Exec,
	core_concepts::dis_detached_tf_opt_token<error_code,basic_stream<Exec>> Token = use_sync_t>
[[nodiscard]] RIWO_EXP_WEBSOCKET_TAPI auto open (
	experimental::http::basic_client<Exec> &http_client, url endpoint, Token &&token = {}
);

template <core_concepts::exec Exec,
	core_concepts::dis_detached_tf_opt_token<error_code,basic_stream<Exec>> Token = use_sync_t>
[[nodiscard]] RIWO_EXP_WEBSOCKET_TAPI auto open (
	experimental::http::basic_client<Exec> &http_client, url endpoint,
	basic_open_diagnostics<Exec> &diagnostics, Token &&token = {}
);

} //namespace riwo::experimental::websocket
#include <riwo/experimental/websocket/detail/client.h>


#endif //RIWO_EXP_WEBSOCKET_CLIENT_H
