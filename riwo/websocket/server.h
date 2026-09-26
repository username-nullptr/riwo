// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_SERVER_H
#define RIWO_WEBSOCKET_SERVER_H

#include <riwo/websocket/stream.h>
#include <riwo/http/server.h>

namespace riwo::websocket
{

struct request_info
{
	http::method_enum method = http::method::get;
	http::version_enum version = http::version::v11;

	std::string target {};
	std::string path {};

	http::headers request_headers {};
	http::parameters query_parameters {};
	http::parameters path_arguments {};

	http::endpoint remote_endpoint {};
	http::endpoint local_endpoint {};
};

struct upgrade_rejection
{
	http::status_enum status = http::status::forbidden;
	http::headers headers {};
	std::string body {};
};

using upgrade_validation_result = optional<upgrade_rejection>;

struct upgrade_options
{
	using request_validator_t = std::function <
		upgrade_validation_result(const request_info&)
	>;
	using origin_validator_t = std::function <
		upgrade_validation_result(optional<std::string_view>)
	>;
	using async_request_validator_t = std::function <
		awaitable<upgrade_validation_result>(const request_info&)
	>;
	using async_origin_validator_t = std::function <
		awaitable<upgrade_validation_result>(optional<std::string>)
	>;
	using subprotocol_selector_t = std::function <
		optional<std::string>(const request_info&, std::span<const std::string>)
	>;
	using extension_selector_t = std::function <
		std::vector<extension>(const request_info&, std::span<const extension>)
	>;
	using async_subprotocol_selector_t = std::function <
		awaitable<optional<std::string>>(
			const request_info&, std::span<const std::string>)
	>;
	using async_extension_selector_t = std::function <
		awaitable<std::vector<extension>>(
			const request_info&, std::span<const extension>)
	>;

	stream_config stream {};
	std::chrono::milliseconds handshake_timeout {30000};

	std::vector<std::string> supported_subprotocols {};
	std::vector<extension> supported_extensions {};

	http::headers response_headers {};
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
	upgrade_options default_upgrade {};
	size_t max_pending_handshakes = 64;
	std::chrono::milliseconds pending_handshake_timeout {30000};
};

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_WEBSOCKET_TAPI basic_accept_result
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using stream_t = basic_stream<executor_t>;

	explicit basic_accept_result(core_concepts::match_sched<executor_t> auto &&exec);
	explicit basic_accept_result(stream_t value);

	basic_accept_result (
		stream_t value, request_info request_value,
		upgrade_result handshake_value = {}
	);
	stream_t stream;
	request_info request {};
	upgrade_result handshake {};
};

using accept_result = basic_accept_result<>;

template <http::concepts::any_exec_stream Stream = asio::ip::tcp::socket>
class RIWO_WEBSOCKET_TAPI basic_server
{
	RIWO_DISABLE_COPY_MOVE(basic_server)

public:
	using socket_t = Stream;
	using executor_type = socket_t::executor_type;
	using executor_t = executor_type;

	using config_t = server_config;
	using stream_t = basic_stream<executor_t>;

	using upgrade_options_t = upgrade_options;
	using upgrade_result_t = upgrade_result;

	using request_info_t = request_info;
	using accept_result_t = basic_accept_result<executor_t>;

	using http_server_t = http::basic_server<socket_t>;
	using http_config_t = http_server_t::config_t;

	using acceptor_wrap_t = http_server_t::acceptor_wrap_t;
	using endpoint_wrapper_t = http_server_t::endpoint_wrapper_t;

	using path_opt_token_t = http_server_t::path_opt_token_t;
	using context_t = http_server_t::context_t;

	using server_error_handler_t = http_server_t::server_error_handler_t;
	using service_error_handler_t = http_server_t::service_error_handler_t;

	template <typename Token>
	static constexpr bool accept_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,accept_result_t>;

	template <typename Func>
	static constexpr bool connection_handler_v = requires(Func &&func, accept_result_t value)
	{
		{ std::forward<Func>(func)(std::move(value)) }
			-> core_concepts::awaitable;

		requires std::same_as <
			awaitable_ret_t<decltype (
				std::forward<Func>(func)(std::move(value))
			)>, void
		>;
	};

public:
	basic_server (
		acceptor_wrap_t &&wrap, core_concepts::sched auto &&service_exec,
		config_t config = {}
	);
	explicit basic_server(acceptor_wrap_t &&wrap, config_t config = {});
	~basic_server();

public:
	basic_server &bind(endpoint_wrapper_t endpoint);
	basic_server &bind(endpoint_wrapper_t endpoint, error_code &error) noexcept;

	basic_server &start(size_t max = asio::socket_base::max_listen_connections);
	basic_server &start(size_t max, error_code &error) noexcept;
	basic_server &start(error_code &error) noexcept;

	basic_server &start (
		core_concepts::sched auto &&service_exec,
		size_t max = asio::socket_base::max_listen_connections
	);
	basic_server &start (
		core_concepts::sched auto &&service_exec,
		size_t max, error_code &error
	) noexcept;

	basic_server &start (
		core_concepts::sched auto &&service_exec,
		error_code &error
	) noexcept;

public:
	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept(Token &&token = {})
		requires accept_token_v<Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept(upgrade_options_t options, Token &&token = {})
		requires accept_token_v<Token>;

	[[nodiscard]] size_t pending_accept_count() const noexcept;
	[[nodiscard]] size_t pending_handshake_count() const noexcept;

public:
	template <typename Func>
	basic_server &on_connection (
		const path_opt_token_t &path_rules, Func &&func,
		optional<upgrade_options_t> options = nullopt
	)
	requires connection_handler_v<Func>;

	template <typename Func>
	basic_server &on_default(Func &&func,
		optional<upgrade_options_t> options = nullopt
	)
	requires connection_handler_v<Func>;

	template <core_concepts::text_p<char> Text>
	basic_server &unbound_connection(const Text &path_rule = "");

	basic_server &on_server_error(server_error_handler_t func);
	basic_server &on_service_error(service_error_handler_t func);

	basic_server &unbound_server_error();
	basic_server &unbound_service_error();

public:
	basic_server &set_config(const config_t &config);
	[[nodiscard]] config_t config() const;

	basic_server &cancel() noexcept;
	basic_server &stop() noexcept;

	[[nodiscard]] const http_server_t &http_server() const noexcept;
	[[nodiscard]] http_server_t &http_server() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_tcp_server = basic_server <
	asio::basic_stream_socket<asio::ip::tcp,Exec>
>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

template <core_concepts::exec Exec>
[[nodiscard]] RIWO_WEBSOCKET_TAPI
bool is_upgrade_request(const http::basic_request<Exec> &request) noexcept;

template <core_concepts::exec Exec, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto upgrade
(http::basic_service_context<Exec> &context, Token &&token = {}) requires
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_accept_result<Exec>>;

template <core_concepts::exec Exec, typename Token = use_sync_t>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto upgrade
(http::basic_service_context<Exec> &context, upgrade_options options, Token &&token = {}) requires
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_accept_result<Exec>>;

} //namespace riwo::websocket
#include <riwo/websocket/detail/server.h>

#if RIWO_OPENSSL_SUPPORT
namespace riwo::websocket
{

template <typename Protocol = asio::ip::tcp, core_concepts::exec Exec = asio::any_io_executor>
using basic_ssl_server = basic_server <
	asio::ssl::stream<asio::basic_stream_socket<Protocol,Exec>>
>;

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_tls_server = basic_ssl_server<asio::ip::tcp,Exec>;

using tls_server = basic_tls_server<>;
using ssl_tcp_server = tls_server;
using ssl_server = tls_server;

} //namespace riwo::websocket

#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_WEBSOCKET_SERVER_H
