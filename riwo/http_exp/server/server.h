// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_SERVER_H
#define RIWO_HTTP_EXP_SERVER_SERVER_H

#include <riwo/http_exp/server/acceptor_wrap.h>
#include <riwo/http_exp/server/config.h>
#include <riwo/http_exp/server/exchange.h>
#include <riwo/http_exp/server/middleware.h>
#include <riwo/http_exp/server/tls.h>
#include <riwo/core/utils/utils.h>

namespace riwo::http_exp { namespace concepts
{

template <typename Func, typename Exec = asio::any_io_executor>
concept request_handler = core_concepts::exec<Exec> and
	requires(Func &&func, basic_server_exchange<Exec> &exchange) {
		{ std::forward<Func>(func)(exchange) } -> core_concepts::awaitable;
	} and
	std::same_as <
		awaitable_ret_t<decltype(std::declval<Func>()(
			std::declval<basic_server_exchange<Exec>&>()))
		>, void
	>;

} //namespace concepts

template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
class RIWO_HTTP_EXP_TAPI basic_server
{
	RIWO_DISABLE_COPY(basic_server)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using service_executor_t = ServiceExec;

	using config_t = server_config;
	using protocol_t = asio::ip::tcp;

	using socket_t = asio::basic_stream_socket<protocol_t,executor_t>;
	using negotiated_transport_t = basic_negotiated_transport<executor_t>;

	using acceptor_wrap_t = basic_acceptor_wrap<executor_t>;
	using acceptor_t = acceptor_wrap_t::acceptor_t;

	using endpoint_wrapper_t = basic_endpoint_wrapper<protocol_t>;
	using context_t = basic_server_exchange<service_executor_t>;
	using request_handler_t = std::function<awaitable<void>(context_t&)>;

	using middleware_t = basic_middleware<service_executor_t>;
	using middleware_ptr = middleware_t::ptr_t;

	using controller_t = basic_controller<service_executor_t>;
	using controller_ptr = controller_t::ptr_t;

	using server_error_handler_t = std::function<bool(error_code)>;
	using service_error_handler_t = std::function<bool(context_t&, const std::exception&)>;

public:
	explicit basic_server(config_t config = {}) requires (
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>
	);

	template <typename Exec0>
	explicit basic_server(Exec0 &&executor, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_server> and
		core_concepts::match_sched<Exec0,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>
	);

	explicit basic_server(acceptor_wrap_t &&acceptor, config_t config = {})
		requires core_concepts::match_sched<executor_t,service_executor_t>;

	template <typename ServiceExec0>
	basic_server(acceptor_wrap_t &&acceptor, ServiceExec0 &&service_executor, config_t config = {})
		requires core_concepts::match_sched<ServiceExec0,service_executor_t>;

#if RIWO_OPENSSL_SUPPORT
	explicit basic_server(server_tls_context_ptr tls_context, config_t config = {}) requires (
		std::same_as<executor_t,asio::any_io_executor> and
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>
	);
#endif //RIWO_OPENSSL_SUPPORT

	basic_server(basic_server &&other) noexcept;
	basic_server &operator=(basic_server &&other) noexcept;

public:
	template <method::well_known...Methods, typename Func, typename...Middleware>
	basic_server &on_request
	(core_concepts::text_p<char> auto &&path, Func &&func, Middleware&&...middleware) requires
		concepts::request_handler<Func,service_executor_t> and
		concepts::middleware_list<service_executor_t,Middleware...>;

	template <typename Func, typename...Middleware>
	basic_server &on_request(method request_method,
		core_concepts::text_p<char> auto &&path, Func &&func, Middleware&&...middleware
	) requires
		concepts::request_handler<Func,service_executor_t> and
		concepts::middleware_list<service_executor_t,Middleware...>;

	template <method::well_known...Methods, core_concepts::text_p<char> Path>
	basic_server &on_request(Path &&path, controller_ptr controller);

	basic_server &on_server_error(server_error_handler_t handler);
	basic_server &on_service_error(service_error_handler_t handler);

	basic_server &use(middleware_ptr middleware);
	basic_server &set_config(config_t config);

	template <typename Func>
	basic_server &on_default(Func &&func) requires
		concepts::request_handler<Func,service_executor_t>;

	basic_server &unbound_request(const core_concepts::text_p<char> auto &path);
	basic_server &unbound_server_error();
	basic_server &unbound_service_error();

public:
	basic_server &bind(endpoint_wrapper_t endpoint);
	basic_server &bind(endpoint_wrapper_t endpoint, error_code &error) noexcept;

	basic_server &start(size_t max = asio::socket_base::max_listen_connections);
	basic_server &start(size_t max, error_code &error) noexcept;
	basic_server &start(error_code &error) noexcept;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor,
		size_t max = asio::socket_base::max_listen_connections
	) requires
		core_concepts::match_sched<ServiceExec0,service_executor_t>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, size_t max, error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_t>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_t>;

	basic_server &serve(negotiated_transport_t transport);
	basic_server &serve(negotiated_transport_t transport, error_code &error) noexcept;

	basic_server &cancel() noexcept;
	basic_server &stop() noexcept;

public:
	[[nodiscard]] config_t config() const noexcept;

	[[nodiscard]] const acceptor_t &acceptor() const noexcept;
	[[nodiscard]] acceptor_t &acceptor() noexcept;

	[[nodiscard]] const acceptor_wrap_t &acceptor_wrap() const noexcept;
	[[nodiscard]] acceptor_wrap_t &acceptor_wrap() noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;
	[[nodiscard]] service_executor_t get_service_executor() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
using basic_tcp_server = basic_server<Exec,ServiceExec>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

} //namespace riwo::http_exp
#include <riwo/http_exp/server/detail/server.h>


#endif //RIWO_HTTP_EXP_SERVER_SERVER_H
