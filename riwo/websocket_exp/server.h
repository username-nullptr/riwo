// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_SERVER_H
#define RIWO_WEBSOCKET_EXP_SERVER_H

#include <riwo/websocket_exp/stream.h>
#include <riwo/http_exp/server.h>

namespace riwo::websocket_exp
{

struct request_info
{
	http_exp::method method {http_exp::method::get};
	http_exp::protocol version {http_exp::protocol::v1_1};
	std::string target {};
	http_exp::fields request_headers {};
	std::string remote_address {};
	uint16_t remote_port = 0;
};

struct upgrade_rejection
{
	http_exp::status status {http_exp::status::forbidden};
	http_exp::fields headers {};
	std::string body {};
};

using upgrade_validation_result = optional<upgrade_rejection>;

struct upgrade_options
{
	using request_validator_t = std::function<upgrade_validation_result(
		const request_info&)>;
	using subprotocol_selector_t = std::function<optional<std::string>(
		const request_info&, std::span<const std::string>)>;
	using extension_selector_t = std::function<std::vector<extension>(
		const request_info&, std::span<const extension>)>;

	stream_config stream {};
	std::chrono::milliseconds handshake_timeout {30000};
	std::vector<std::string> supported_subprotocols {};
	std::vector<extension> supported_extensions {};
	http_exp::fields response_headers {};
	bool require_subprotocol = false;
	request_validator_t request_validator {};
	subprotocol_selector_t subprotocol_selector {};
	extension_selector_t extension_selector {};
};

struct upgrade_result
{
	std::string subprotocol {};
	std::vector<extension> extensions {};
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct RIWO_WEBSOCKET_EXP_TAPI basic_accept_result
{
	template <typename Exec0>
	explicit basic_accept_result(Exec0 &&executor) requires
		core_concepts::match_sched<Exec0,Exec> :
		stream(get_executor_helper(std::forward<Exec0>(executor))) {}

	basic_stream<Exec> stream;
	request_info request {};
	upgrade_result handshake {};
};

using accept_result = basic_accept_result<>;

template <core_concepts::exec Exec>
[[nodiscard]] RIWO_WEBSOCKET_EXP_TAPI bool is_upgrade_request(
	const http_exp::basic_server_exchange<Exec> &exchange) noexcept;

template <core_concepts::exec Exec, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_EXP_TAPI auto upgrade(
	http_exp::basic_server_exchange<Exec> &exchange, Token &&token = {})
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>>;

template <core_concepts::exec Exec, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_EXP_TAPI auto upgrade(
	http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, Token &&token = {})
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>>;

} //namespace riwo::websocket_exp

#include <riwo/websocket_exp/detail/server.h>

#endif //RIWO_WEBSOCKET_EXP_SERVER_H
