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
	std::string path {};
	http_exp::fields request_headers {};
	parameter_map query_parameters {};
	parameter_map path_arguments {};
	asio::ip::tcp::endpoint remote_endpoint {};
	asio::ip::tcp::endpoint local_endpoint {};
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
	using origin_validator_t = std::function<upgrade_validation_result(
		optional<std::string_view>)>;
	using async_request_validator_t = std::function<
		awaitable<upgrade_validation_result>(const request_info&)>;
	using async_origin_validator_t = std::function<
		awaitable<upgrade_validation_result>(optional<std::string>)>;
	using subprotocol_selector_t = std::function<optional<std::string>(
		const request_info&, std::span<const std::string>)>;
	using extension_selector_t = std::function<std::vector<extension>(
		const request_info&, std::span<const extension>)>;
	using async_subprotocol_selector_t = std::function<
		awaitable<optional<std::string>>(
			const request_info&, std::span<const std::string>)>;
	using async_extension_selector_t = std::function<
		awaitable<std::vector<extension>>(
			const request_info&, std::span<const extension>)>;

	stream_config stream {};
	std::chrono::milliseconds handshake_timeout {30000};
	std::vector<std::string> supported_subprotocols {};
	std::vector<extension> supported_extensions {};
	http_exp::fields response_headers {};
	bool require_subprotocol = false;
	request_validator_t request_validator {};
	origin_validator_t origin_validator {};
	async_request_validator_t async_request_validator {};
	async_origin_validator_t async_origin_validator {};
	subprotocol_selector_t subprotocol_selector {};
	extension_selector_t extension_selector {};
	async_subprotocol_selector_t async_subprotocol_selector {};
	async_extension_selector_t async_extension_selector {};
};

struct upgrade_result
{
	std::string subprotocol {};
	std::vector<extension> extensions {};
};

struct server_config
{
	http_exp::server_config http {};
	upgrade_options default_upgrade {};
	size_t max_pending_handshakes = 64;
	std::chrono::milliseconds pending_handshake_timeout {30000};
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct RIWO_WEBSOCKET_EXP_TAPI basic_accept_result
{
	using executor_type = Exec;
	using executor_t = executor_type;

	template <typename Exec0>
	explicit basic_accept_result(Exec0 &&executor) requires
		core_concepts::match_sched<Exec0,Exec> :
		stream(get_executor_helper(std::forward<Exec0>(executor))) {}
	explicit basic_accept_result(basic_stream<Exec> value) :
		stream(std::move(value)) {}
	basic_accept_result(basic_stream<Exec> value, request_info request_value,
		upgrade_result handshake_value = {}) :
		stream(std::move(value)), request(std::move(request_value)),
		handshake(std::move(handshake_value)) {}

	basic_stream<Exec> stream;
	request_info request {};
	upgrade_result handshake {};
};

using accept_result = basic_accept_result<>;

template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
class RIWO_WEBSOCKET_EXP_TAPI basic_server
{
	RIWO_DISABLE_COPY_MOVE(basic_server)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using service_executor_t = ServiceExec;
	using config_t = server_config;
	using stream_t = basic_stream<service_executor_t>;
	using upgrade_options_t = upgrade_options;
	using upgrade_result_t = upgrade_result;
	using request_info_t = request_info;
	using accept_result_t = basic_accept_result<service_executor_t>;
	using http_server_t = http_exp::basic_server<executor_t,service_executor_t>;
	using http_config_t = typename http_server_t::config_t;
	using acceptor_wrap_t = typename http_server_t::acceptor_wrap_t;
	using endpoint_wrapper_t = typename http_server_t::endpoint_wrapper_t;
	using context_t = typename http_server_t::context_t;
	using server_error_handler_t = typename http_server_t::server_error_handler_t;
	using service_error_handler_t = typename http_server_t::service_error_handler_t;
	using transport_listener_t = http_exp::basic_transport_listener<executor_t>;

	template <typename Token>
	static constexpr bool accept_token_v =
		core_concepts::dis_detached_tf_opt_token<
			Token,error_code,accept_result_t>;

	template <typename Func>
	static constexpr bool connection_handler_v = requires(
		Func &&func, accept_result_t value) {
		{ std::forward<Func>(func)(std::move(value)) }
			-> core_concepts::awaitable;
		requires std::same_as<awaitable_ret_t<decltype(
			std::forward<Func>(func)(std::move(value)))>,void>;
	};

	basic_server() requires (
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>);
	explicit basic_server(acceptor_wrap_t &&acceptor, config_t config = {})
		requires core_concepts::match_sched<executor_t,service_executor_t>;

	template <typename ServiceExec0>
	basic_server(acceptor_wrap_t &&acceptor, ServiceExec0 &&service_executor,
		config_t config = {}) requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	explicit basic_server(std::shared_ptr<transport_listener_t> listener,
		config_t config = {}) requires
		core_concepts::match_sched<executor_t,service_executor_t>;

	template <typename ServiceExec0>
	basic_server(std::shared_ptr<transport_listener_t> listener,
		ServiceExec0 &&service_executor, config_t config = {}) requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	~basic_server();

	basic_server &bind(endpoint_wrapper_t endpoint);
	basic_server &bind(endpoint_wrapper_t endpoint,
		error_code &error) noexcept;
	basic_server &start(size_t maximum =
		asio::socket_base::max_listen_connections);
	basic_server &start(size_t maximum, error_code &error) noexcept;
	basic_server &start(error_code &error) noexcept;
	basic_server &serve(http_exp::basic_negotiated_transport<executor_t> transport);
	basic_server &serve(http_exp::basic_negotiated_transport<executor_t> transport,
		error_code &error) noexcept;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor,
		size_t maximum = asio::socket_base::max_listen_connections) requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, size_t maximum,
		error_code &error) noexcept requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor,
		error_code &error) noexcept requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept(Token &&token = {})
		requires accept_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept(upgrade_options_t options, Token &&token = {})
		requires accept_token_v<Token>;

	[[nodiscard]] size_t pending_accept_count() const noexcept;
	[[nodiscard]] size_t pending_handshake_count() const noexcept;

	template <typename Func>
	basic_server &on_connection(std::string path, Func &&func,
		optional<upgrade_options_t> options = nullopt)
		requires connection_handler_v<Func>;

	template <typename Func>
	basic_server &on_default(Func &&func,
		optional<upgrade_options_t> options = nullopt)
		requires connection_handler_v<Func>;

	basic_server &unbound_connection(std::string_view path = {});
	basic_server &on_server_error(server_error_handler_t handler);
	basic_server &on_service_error(service_error_handler_t handler);
	basic_server &unbound_server_error();
	basic_server &unbound_service_error();
	basic_server &set_config(config_t config);
	[[nodiscard]] config_t config() const;
	basic_server &cancel() noexcept;
	basic_server &stop() noexcept;
	[[nodiscard]] const http_server_t &http_server() const noexcept;
	[[nodiscard]] http_server_t &http_server() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;
	[[nodiscard]] service_executor_t get_service_executor() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_tcp_server = basic_server<Exec,Exec>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

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
