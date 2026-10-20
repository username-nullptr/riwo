// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_CLIENT_H
#define RIWO_WEBSOCKET_EXP_CLIENT_H

#include <riwo/websocket_exp/stream.h>
#include <riwo/http_exp/client.h>

namespace riwo::websocket_exp
{

struct client_config
{
	stream_config stream {};
	http_exp::client_config http {};
	std::chrono::milliseconds handshake_timeout {30000};
};

struct connect_request
{
	url endpoint {};
	http_exp::fields headers {};
	optional<stream_config> stream_options {};
	optional<std::chrono::milliseconds> handshake_timeout {};
	std::vector<std::string> subprotocols {};
	std::vector<extension> extensions {};
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct basic_open_diagnostics
{
	using executor_type = Exec;
	using executor_t = executor_type;

	url endpoint {};
	optional<http_exp::response_head> response {};
	optional<http_exp::protocol> selected_protocol {};
};

using open_diagnostics = basic_open_diagnostics<>;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_WEBSOCKET_EXP_TAPI basic_client
{
	RIWO_DISABLE_COPY(basic_client)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using stream_t = basic_stream<executor_type>;
	using config_t = client_config;
	using connect_request_t = connect_request;
	using http_client_t = http_exp::basic_client<executor_type>;
	using diagnostics_t = basic_open_diagnostics<executor_type>;

	template <typename Token>
	static constexpr bool open_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,stream_t>;

	basic_client() requires
		core_concepts::match_sched<io_executor_t,executor_type>;
	explicit basic_client(config_t config) requires
		core_concepts::match_sched<io_executor_t,executor_type>;

	template <typename Exec0>
	explicit basic_client(Exec0 &&executor, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_type>);

	basic_client(basic_client &&) noexcept = default;
	basic_client &operator=(basic_client &&) noexcept = default;
	~basic_client() = default;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(connect_request_t request, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(connect_request_t request, diagnostics_t &diagnostics,
		Token &&token = {}) requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(url endpoint, Token &&token = {})
		requires open_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(url endpoint, diagnostics_t &diagnostics,
		Token &&token = {}) requires open_token_v<Token>;

	[[nodiscard]] const config_t &config() const noexcept;
	[[nodiscard]] const http_client_t &http_client() const noexcept;
	[[nodiscard]] http_client_t &http_client() noexcept;
	[[nodiscard]] executor_type get_executor() const noexcept;
	void cancel() noexcept;

public:
	class impl;

private:
	template <typename Token>
	[[nodiscard]] auto open_impl(connect_request_t request,
		diagnostics_t *diagnostics, Token &&token);

	std::shared_ptr<impl> m_impl;
};

using client = basic_client<>;

} //namespace riwo::websocket_exp

#include <riwo/websocket_exp/detail/client.h>

#endif //RIWO_WEBSOCKET_EXP_CLIENT_H
