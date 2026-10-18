// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_SERVER_H
#define RIWO_HTTP_EXP_SERVER_SERVER_H

#include <riwo/http_exp/server/acceptor_wrap.h>
#include <riwo/http_exp/server/config.h>
#include <riwo/http_exp/server/exchange.h>
#include <riwo/core/utils/utils.h>

namespace riwo::http_exp
{

namespace concepts
{

template <typename Func, typename Exec = asio::any_io_executor>
concept request_handler = core_concepts::exec<Exec> &&
	requires(Func &&func, basic_server_exchange<Exec> &exchange) {
	{ std::forward<Func>(func)(exchange) } -> core_concepts::awaitable;
} && std::same_as <
	awaitable_ret_t<decltype(std::declval<Func>()(
		std::declval<basic_server_exchange<Exec>&>()))>, void
>;

} //namespace concepts

/**
 * Protocol-independent server facade.
 *
 * The fluent surface intentionally follows the stable HTTP server.  Routing
 * targets a server_exchange rather than a connection-bound service context,
 * so the same handler can run on an HTTP/1 request or an HTTP/2 stream.
 *
 * Stage 0 fixes the public contract and route ownership.  start() reports
 * operation_not_supported until the listener and protocol Session factory are
 * connected in stages B-E.
 */
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
		requires core_concepts::match_sched<executor_type,service_executor_type> :
		acceptor_wrap_(std::move(acceptor)),
		service_executor_(acceptor_wrap_.get_executor()),
		config_(std::move(config))
	{
	}

	template <typename ServiceExec0>
	basic_server(acceptor_wrap_type &&acceptor, ServiceExec0 &&service_executor,
		config_type config = {})
		requires core_concepts::match_sched<ServiceExec0,service_executor_type> :
		acceptor_wrap_(std::move(acceptor)),
		service_executor_(get_executor_helper(
			std::forward<ServiceExec0>(service_executor))),
		config_(std::move(config))
	{
	}

	[[nodiscard]] executor_type get_executor() noexcept {
		return acceptor_wrap_.get_executor();
	}

	[[nodiscard]] service_executor_type get_service_executor() const noexcept {
		return service_executor_;
	}

	basic_server &bind(endpoint_wrapper_type endpoint)
	{
		error_code error;
		bind(std::move(endpoint), error);
		if( error )
			throw std::system_error(std::error_code(error));
		return *this;
	}

	basic_server &bind(endpoint_wrapper_type endpoint, error_code &error) noexcept
	{
		auto &acceptor = acceptor_wrap_.acceptor();
		if( !acceptor.is_open() )
		{
			acceptor.open(endpoint->protocol(), error);
			if( error )
				return *this;
		}
		acceptor.set_option(asio::socket_base::reuse_address(true), error);
		if( error )
			return *this;

		acceptor.bind(*endpoint, error);
		return *this;
	}

	template <method::well_known... Methods, typename Func>
	basic_server &on_request(std::string path, Func &&func)
		requires concepts::request_handler<Func,service_executor_type>
	{
		register_route(std::move(path),
			std::vector<http_exp::method>{http_exp::method(Methods)...},
			request_handler_type(std::forward<Func>(func)));
		return *this;
	}

	template <typename Func>
	basic_server &on_request(http_exp::method request_method,
		std::string path, Func &&func)
		requires concepts::request_handler<Func,service_executor_type>
	{
		register_route(std::move(path),
			std::vector<http_exp::method>{std::move(request_method)},
			request_handler_type(std::forward<Func>(func)));
		return *this;
	}

	template <typename Func>
	basic_server &on_default(Func &&func)
		requires concepts::request_handler<Func,service_executor_type>
	{
		default_handler_ = request_handler_type(std::forward<Func>(func));
		return *this;
	}

	basic_server &on_server_error(server_error_handler_type handler)
	{
		server_error_handler_ = std::move(handler);
		return *this;
	}

	basic_server &on_service_error(service_error_handler_type handler)
	{
		service_error_handler_ = std::move(handler);
		return *this;
	}

	basic_server &unbound_request(std::string_view path)
	{
		std::erase_if(routes_, [path](const route &value) {
			return value.path == path;
		});
		return *this;
	}

	basic_server &unbound_server_error()
	{
		server_error_handler_ = {};
		return *this;
	}

	basic_server &unbound_service_error()
	{
		service_error_handler_ = {};
		return *this;
	}

	basic_server &set_config(config_type config)
	{
		config_ = std::move(config);
		return *this;
	}

	[[nodiscard]] config_type config() const noexcept {
		return config_;
	}

	basic_server &start(size_t = asio::socket_base::max_listen_connections)
	{
		throw std::system_error(std::error_code(
			make_error_code(errc::operation_not_supported)));
	}

	basic_server &start(size_t, error_code &error) noexcept
	{
		error = make_error_code(errc::operation_not_supported);
		return *this;
	}

	basic_server &start(error_code &error) noexcept
	{
		return start(asio::socket_base::max_listen_connections, error);
	}

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor,
		size_t max = asio::socket_base::max_listen_connections)
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>
	{
		service_executor_ = get_executor_helper(
			std::forward<ServiceExec0>(service_executor));
		return start(max);
	}

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, size_t max,
		error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>
	{
		service_executor_ = get_executor_helper(
			std::forward<ServiceExec0>(service_executor));
		return start(max, error);
	}

	template <typename ServiceExec0>
	basic_server &start(ServiceExec0 &&service_executor, error_code &error) noexcept
		requires core_concepts::match_sched<ServiceExec0,service_executor_type>
	{
		return start(std::forward<ServiceExec0>(service_executor),
			asio::socket_base::max_listen_connections, error);
	}

	basic_server &cancel() noexcept
	{
		error_code ignored;
		acceptor_wrap_.acceptor().cancel(ignored);
		return *this;
	}

	basic_server &stop() noexcept {
		return cancel();
	}

	[[nodiscard]] const acceptor_type &acceptor() const noexcept {
		return acceptor_wrap_.acceptor();
	}

	[[nodiscard]] acceptor_type &acceptor() noexcept {
		return acceptor_wrap_.acceptor();
	}

	[[nodiscard]] const acceptor_wrap_type &acceptor_wrap() const noexcept {
		return acceptor_wrap_;
	}

	[[nodiscard]] acceptor_wrap_type &acceptor_wrap() noexcept {
		return acceptor_wrap_;
	}

private:
	struct route
	{
		std::string path;
		// An empty list means every well-known and extension method.
		std::vector<http_exp::method> methods;
		request_handler_type handler;
	};

	void register_route(std::string path, std::vector<http_exp::method> methods,
		request_handler_type handler)
	{
		if( path.empty() )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: path is empty");
		if( !handler )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: handler is empty");
		if( std::ranges::any_of(routes_, [&path](const route &value) {
			return value.path == path;
		}))
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: path is already bound");

		routes_.push_back(route{
			std::move(path), std::move(methods), std::move(handler)
		});
	}

	acceptor_wrap_type acceptor_wrap_;
	service_executor_type service_executor_;
	config_type config_;
	std::vector<route> routes_;
	request_handler_type default_handler_;
	server_error_handler_type server_error_handler_;
	service_error_handler_type service_error_handler_;
};

template <core_concepts::exec Exec = asio::any_io_executor,
	core_concepts::exec ServiceExec = Exec>
using basic_tcp_server = basic_server<Exec,ServiceExec>;

using tcp_server = basic_tcp_server<>;
using server = tcp_server;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_SERVER_H
