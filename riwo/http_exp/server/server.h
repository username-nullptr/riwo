// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_SERVER_H
#define RIWO_HTTP_EXP_SERVER_SERVER_H

#include <riwo/http_exp/server/acceptor_wrap.h>
#include <riwo/http_exp/server/config.h>
#include <riwo/http_exp/server/exchange.h>
#include <riwo/http_exp/transport/listener.h>
#include <riwo/core/utils/utils.h>

namespace riwo::http_exp
{

namespace concepts
{

template <typename Func, typename Exec = asio::any_io_executor>
concept request_handler = core_concepts::exec<Exec> and
	requires(Func &&func, basic_server_exchange<Exec> &exchange) {
	{ std::forward<Func>(func)(exchange) } -> core_concepts::awaitable;
} and std::same_as <
	awaitable_ret_t<decltype(std::declval<Func>()(
		std::declval<basic_server_exchange<Exec>&>()))>, void
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
	using service_executor_type = ServiceExec;
	using service_executor_t = service_executor_type;
	using config_type = server_config;
	using config_t = config_type;
	using protocol_type = asio::ip::tcp;
	using socket_type = asio::basic_stream_socket<protocol_type,executor_type>;
	using socket_t = socket_type;
	using acceptor_wrap_type = basic_acceptor_wrap<executor_type>;
	using acceptor_wrap_t = acceptor_wrap_type;
	using acceptor_type = typename acceptor_wrap_type::acceptor_type;
	using acceptor_t = acceptor_type;
	using endpoint_wrapper_type = basic_endpoint_wrapper<protocol_type>;
	using endpoint_wrapper_t = endpoint_wrapper_type;
	using context_type = basic_server_exchange<service_executor_type>;
	using context_t = context_type;
	using request_handler_type = std::function<awaitable<void>(context_type&)>;
	using request_handler_t = request_handler_type;
	using server_error_handler_type = std::function<bool(error_code)>;
	using server_error_handler_t = server_error_handler_type;
	using service_error_handler_type =
		std::function<bool(context_type&, const std::exception&)>;
	using service_error_handler_t = service_error_handler_type;

	basic_server(basic_server &&) noexcept = default;
	basic_server &operator=(basic_server &&) noexcept = default;

	explicit basic_server(acceptor_wrap_type &&acceptor,
		config_type config = {})
		requires core_concepts::match_sched<executor_type,service_executor_type>;

	template <typename ServiceExec0>
	basic_server(acceptor_wrap_type &&acceptor, ServiceExec0 &&service_executor,
		config_type config = {})
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>;

	explicit basic_server(
		std::shared_ptr<basic_transport_listener<executor_type>> listener,
		config_type config = {})
		requires core_concepts::match_sched<executor_type,service_executor_type>;

	template <typename ServiceExec0>
	basic_server(
		std::shared_ptr<basic_transport_listener<executor_type>> listener,
		ServiceExec0 &&service_executor, config_type config = {})
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>;

	[[nodiscard]] executor_type get_executor() noexcept;
	[[nodiscard]] service_executor_type get_service_executor() const noexcept;
	basic_server &bind(endpoint_wrapper_type endpoint);
	basic_server &bind(endpoint_wrapper_type endpoint, error_code &error) noexcept;

	template <method::well_known... Methods, typename Func>
	basic_server &on_request(std::string path, Func &&func)
		requires concepts::request_handler<Func,service_executor_type>;

	template <typename Func>
	basic_server &on_request(http_exp::method request_method,
		std::string path, Func &&func)
		requires concepts::request_handler<Func,service_executor_type>;

	template <typename Func>
	basic_server &on_default(Func &&func)
		requires concepts::request_handler<Func,service_executor_type>;

	basic_server &on_server_error(server_error_handler_type handler);
	basic_server &on_service_error(service_error_handler_type handler);
	basic_server &unbound_request(std::string_view path);
	basic_server &unbound_server_error();
	basic_server &unbound_service_error();
	basic_server &set_config(config_type config);
	[[nodiscard]] config_type config() const noexcept;
	basic_server &start(size_t max = asio::socket_base::max_listen_connections);
	basic_server &start(size_t max, error_code &error) noexcept;
	basic_server &start(error_code &error) noexcept;
	basic_server &serve(basic_negotiated_transport<executor_type> transport);
	basic_server &serve(basic_negotiated_transport<executor_type> transport,
		error_code &error) noexcept;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor,
		size_t max = asio::socket_base::max_listen_connections)
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, size_t max,
		error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>;

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>;

	basic_server &cancel() noexcept;
	basic_server &stop() noexcept;
	[[nodiscard]] const acceptor_type &acceptor() const noexcept;
	[[nodiscard]] acceptor_type &acceptor() noexcept;
	[[nodiscard]] const acceptor_wrap_type &acceptor_wrap() const noexcept;
	[[nodiscard]] acceptor_wrap_type &acceptor_wrap() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
using basic_tcp_server = basic_server<Exec,ServiceExec>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

} //namespace riwo::http_exp
#include <riwo/http_exp/server/detail/server.h>


#endif //RIWO_HTTP_EXP_SERVER_SERVER_H
