// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H

namespace riwo::http_exp
{

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_type &&acceptor,
	config_type config)
	requires core_concepts::match_sched<executor_type,service_executor_type> :
	acceptor_wrap_(std::move(acceptor)),
	service_executor_(acceptor_wrap_.get_executor()),
	config_(std::move(config))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_type &&acceptor,
	ServiceExec0 &&service_executor, config_type config)
	requires core_concepts::match_sched<ServiceExec0,service_executor_type> :
	acceptor_wrap_(std::move(acceptor)),
	service_executor_(get_executor_helper(std::forward<ServiceExec0>(service_executor))),
	config_(std::move(config))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_executor() noexcept -> executor_type {
	return acceptor_wrap_.get_executor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_service_executor() const noexcept
	-> service_executor_type {
	return service_executor_;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_type endpoint)
	-> basic_server&
{
	error_code error;
	bind(std::move(endpoint), error);
	if( error ) throw std::system_error(std::error_code(error));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_type endpoint,
	error_code &error) noexcept -> basic_server&
{
	auto &value = acceptor_wrap_.acceptor();
	if( !value.is_open() )
	{
		value.open(endpoint->protocol(), error);
		if( error ) return *this;
	}
	value.set_option(asio::socket_base::reuse_address(true), error);
	if( error ) return *this;
	value.bind(*endpoint, error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <method::well_known... Methods, typename Func>
auto basic_server<Exec,ServiceExec>::on_request(std::string path, Func &&func)
	-> basic_server&
	requires concepts::request_handler<Func,service_executor_type>
{
	register_route(std::move(path),
		std::vector<http_exp::method>{http_exp::method(Methods)...},
		request_handler_type(std::forward<Func>(func)));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_request(http_exp::method request_method,
	std::string path, Func &&func) -> basic_server&
	requires concepts::request_handler<Func,service_executor_type>
{
	register_route(std::move(path),
		std::vector<http_exp::method>{std::move(request_method)},
		request_handler_type(std::forward<Func>(func)));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_default(Func &&func) -> basic_server&
	requires concepts::request_handler<Func,service_executor_type>
{
	default_handler_ = request_handler_type(std::forward<Func>(func));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_server_error(
	server_error_handler_type handler) -> basic_server&
{
	server_error_handler_ = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_service_error(
	service_error_handler_type handler) -> basic_server&
{
	service_error_handler_ = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_request(std::string_view path)
	-> basic_server&
{
	std::erase_if(routes_, [path](const route &value) {
		return value.path == path;
	});
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_server_error() -> basic_server& {
	server_error_handler_ = {};
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_service_error() -> basic_server& {
	service_error_handler_ = {};
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::set_config(config_type config)
	-> basic_server&
{
	config_ = std::move(config);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::config() const noexcept -> config_type {
	return config_;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t) -> basic_server&
{
	throw std::system_error(std::error_code(
		make_error_code(errc::operation_not_supported)));
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t, error_code &error) noexcept
	-> basic_server&
{
	error = make_error_code(errc::operation_not_supported);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(error_code &error) noexcept
	-> basic_server&
{
	return start(asio::socket_base::max_listen_connections, error);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t max) -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_type>
{
	service_executor_ = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(max);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t max, error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_type>
{
	service_executor_ = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(max, error);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_type>
{
	return start(std::forward<ServiceExec0>(service_executor),
		asio::socket_base::max_listen_connections, error);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::cancel() noexcept -> basic_server&
{
	error_code ignored;
	acceptor_wrap_.acceptor().cancel(ignored);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::stop() noexcept -> basic_server& {
	return cancel();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor() const noexcept
	-> const acceptor_type& {
	return acceptor_wrap_.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor() noexcept -> acceptor_type& {
	return acceptor_wrap_.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() const noexcept
	-> const acceptor_wrap_type& {
	return acceptor_wrap_;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() noexcept
	-> acceptor_wrap_type& {
	return acceptor_wrap_;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
void basic_server<Exec,ServiceExec>::register_route(std::string path,
	std::vector<http_exp::method> methods, request_handler_type handler)
{
	if( path.empty() )
		throw std::invalid_argument(
			"riwo::http_exp::server::on_request: path is empty");
	if( !handler )
		throw std::invalid_argument(
			"riwo::http_exp::server::on_request: handler is empty");
	if( std::ranges::any_of(routes_, [&path](const route &value) {
		return value.path == path;
	}) )
		throw std::invalid_argument(
			"riwo::http_exp::server::on_request: path is already bound");
	routes_.push_back(route{
		std::move(path), std::move(methods), std::move(handler)
	});
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
