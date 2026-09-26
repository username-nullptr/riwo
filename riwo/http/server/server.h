// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_SERVER_H
#define RIWO_HTTP_SERVER_SERVER_H

#include <riwo/http/server/acceptor_wrap.h>
#include <riwo/http/server/aop.h>

namespace riwo::http
{

template <concepts::any_exec_stream>
struct server_config;

template <core_concepts::exec Exec>
struct server_config<asio::basic_stream_socket<asio::ip::tcp,Exec>>
{
	std::chrono::milliseconds first_reading_time {1500};
	std::chrono::milliseconds keepalive_time {5000};

	// Base for relative server-side file paths. Empty keeps the executable
	// directory as the base for backward compatibility.
	std::filesystem::path resource_root {};
};

#if RIWO_OPENSSL_SUPPORT
template <core_concepts::exec Exec>
struct server_config<asio::ssl::stream<asio::basic_stream_socket<asio::ip::tcp,Exec>>> :
	server_config<asio::basic_stream_socket<asio::ip::tcp,Exec>>
{
	std::chrono::milliseconds tls_handshake_timeout {5000};
};
#endif //RIWO_OPENSSL_SUPPORT

template <concepts::any_exec_stream Stream = asio::ip::tcp::socket>
class RIWO_HTTP_TAPI basic_server
{
	RIWO_DISABLE_COPY_MOVE(basic_server)

public:
	using socket_t = Stream;
	using executor_type = socket_t::executor_type;

	using executor_t = executor_type;
	using config_t = server_config<socket_t>;

	using acceptor_wrap_t = basic_acceptor_wrap<socket_t>;
	using acceptor_t = acceptor_wrap_t::acceptor_t;

	using connection_t = acceptor_wrap_t::connection_t;
	using connection_ptr = acceptor_wrap_t::connection_ptr;

	using endpoint_wrapper_t = basic_endpoint_wrapper <
		typename acceptor_t::protocol_type
	>;
	using request_t = basic_request<executor_t>;
	using response_t = basic_response<executor_t>;

	using path_opt_token_t = basic_path_opt_token<char>;
	using context_t = basic_service_context<executor_t>;

	using aop_t = basic_aop<executor_t>;
	using ctrlr_aop_t = basic_ctrlr_aop<executor_t>;

	using aop_ptr_t = basic_aop<executor_t>::ptr_t;
	using ctrlr_aop_ptr_t = basic_ctrlr_aop<executor_t>::ptr_t;

	using server_error_handler_t = std::function<bool(error_code)>;
	using service_error_handler_t = std::function<bool(context_t&, const std::exception&)>;

public:
	basic_server(acceptor_wrap_t &&wrap, core_concepts::sched auto &&service_exec);
	basic_server(acceptor_wrap_t &&wrap);
	~basic_server();

	basic_server &bind(endpoint_wrapper_t ep);
	basic_server &bind(endpoint_wrapper_t ep, error_code &error) noexcept;

public:
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
		core_concepts::sched auto service_exec,
		error_code &error
	) noexcept;

public:
	template <method_enum...Method, typename Func, typename...AopPtrs>
	basic_server &on_request(const path_opt_token_t &path_rules, Func &&func, AopPtrs&&...aops) requires
		concepts::request_handler<Func,executor_t> and
		concepts::aop_ptr_list<executor_t,AopPtrs...>;

	template <method_enum...Method>
	basic_server &on_request(const path_opt_token_t &path_rules, ctrlr_aop_ptr_t ctrlr);

	template <method_enum...Method>
	basic_server &on_request(const path_opt_token_t &path_rules, ctrlr_aop_t *ctrlr);

	template <typename Func>
	basic_server &on_default(Func &&func) requires
		concepts::request_handler<Func,executor_t>;

public:
	basic_server &on_server_error(server_error_handler_t func);
	basic_server &on_service_error(service_error_handler_t func);

	template <core_concepts::text_p<char> Text>
	basic_server &unbound_request(const Text &path_rule = "");

	basic_server &unbound_server_error();
	basic_server &unbound_service_error();

public:
	basic_server &set_config(const config_t &config);
	[[nodiscard]] config_t config() const noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;
	basic_server &cancel() noexcept;
	basic_server &stop() noexcept;

public:
	[[nodiscard]] const acceptor_wrap_t &acceptor_wrap() const;
	[[nodiscard]] acceptor_wrap_t &acceptor_wrap();

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_tcp_server = basic_server<asio::basic_stream_socket<asio::ip::tcp,Exec>>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

} //namespace riwo::http
#include <riwo/http/server/detail/server.h>

#if RIWO_OPENSSL_SUPPORT
namespace riwo { namespace http
{

template <typename Protocol = asio::ip::tcp,
		  core_concepts::exec Exec = asio::any_io_executor>
using basic_ssl_server = basic_server <
	asio::ssl::stream<asio::basic_stream_socket<Protocol,Exec>>
>;

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_tls_server = basic_ssl_server<asio::ip::tcp,Exec>;

using tls_server = basic_tls_server<>;
using ssl_tcp_server = tls_server;
using ssl_server = tls_server;

} //namespace riwo::http

namespace https
{

template <typename Protocol = asio::ip::tcp,
		  concepts::exec Exec = asio::any_io_executor>
using basic_server = http::basic_server <
	asio::ssl::stream<asio::basic_stream_socket<Protocol,Exec>>
>;
using tcp_server = basic_server<>;
using server = tcp_server;

}} //namespace riwo::https

#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_HTTP_SERVER_SERVER_H
