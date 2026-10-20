// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H

#include <riwo/http_exp/server/detail/session.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
class basic_server<Exec,ServiceExec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	struct route
	{
		std::string path;
		std::vector<http_exp::method> methods;
		request_handler_type handler;
	};

	impl(acceptor_wrap_type &&acceptor_value,
		service_executor_type service_executor_value, config_type config_value) :
		m_acceptor_wrap(std::move(acceptor_value)),
		m_service_executor(std::move(service_executor_value)),
		m_config(std::move(config_value))
	{
	}

	impl(acceptor_wrap_type &&acceptor_value, config_type config_value) :
		m_acceptor_wrap(std::move(acceptor_value)),
		m_service_executor(m_acceptor_wrap.get_executor()),
		m_config(std::move(config_value))
	{
	}

	void start(size_t maximum, error_code &error) noexcept
	{
		error.clear();
		if( m_running ) return;
		error = detail::validate_server_config(m_config);
		if( error ) return;
		m_acceptor_wrap.acceptor().listen(static_cast<int>(std::min<size_t>(
			maximum, static_cast<size_t>(std::numeric_limits<int>::max()))), error);
		if( error ) return;
		m_running = true;
		accept_next();
	}

	void stop() noexcept
	{
		m_running = false;
		error_code ignored;
		m_acceptor_wrap.acceptor().cancel(ignored);
		m_acceptor_wrap.acceptor().close(ignored);
		for( auto &value : m_connections )
			if( value ) value->cancel();
		m_connections.clear();
	}

	void register_route(std::string path,
		std::vector<http_exp::method> methods, request_handler_type handler)
	{
		if( path.empty() )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: path is empty");
		if( not handler )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: handler is empty");
		if( std::ranges::any_of(m_routes, [&path](const route &value) {
			return value.path == path;
		}) )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: path is already bound");
		m_routes.push_back(route{
			std::move(path), std::move(methods), std::move(handler)
		});
	}

	void report_server_error(error_code error) noexcept
	{
		if( error == asio::error::make_error_code(asio::error::operation_aborted) )
			return;
		try {
			if( m_server_error_handler ) (void)m_server_error_handler(error);
		}
		catch(...) {
		}
	}

private:
	void accept_next()
	{
		if( not m_running ) return;
		auto socket = std::make_shared<asio::ip::tcp::socket>(
			asio::any_io_executor(m_acceptor_wrap.acceptor().get_executor()));
		auto self = this->shared_from_this();
		m_acceptor_wrap.acceptor().async_accept(*socket,
		[self, socket]
		(error_code error) mutable {
			if( not self->m_running ) return;
			self->accept_next();
			if( error )
			{
				self->report_server_error(error);
				return;
			}
			auto connection = detail::accept_server(std::move(*socket),
				self->m_config,
			[self]
			(error_code accept_error, std::shared_ptr<detail::stream> stream,
				request_head request) mutable {
				if( accept_error )
				{
					self->report_server_error(accept_error);
					return;
				}
				self->serve(std::move(stream), std::move(request));
			});
			std::erase_if(self->m_connections,
				[](const detail::server_connection_ptr &value) {
					return not value or value->closed();
				});
			self->m_connections.push_back(std::move(connection));
		});
	}

	void serve(std::shared_ptr<detail::stream> stream, request_head request)
	{
		auto self = this->shared_from_this();
		riwo::dispatch(m_service_executor,
		[self = std::move(self), stream = std::move(stream),
		 request = std::move(request)]
		() mutable -> awaitable<void>
		{
			context_type exchange(self->m_service_executor, stream,
				std::move(request));
			try
			{
				auto target = std::string_view(exchange.request().target);
				if( auto query = target.find('?'); query != std::string_view::npos )
					target = target.substr(0, query);

				auto method_list = [](const std::vector<http_exp::method> &values) {
					std::string result;
					for( const auto &value : values )
					{
						if( not result.empty() ) result += ", ";
						result += value.text();
					}
					return result;
				};
				auto all_methods = [self]() {
					std::vector<http_exp::method> values;
					for( const auto &route : self->m_routes )
						for( const auto &value : route.methods )
							if( std::ranges::none_of(values,
								[&value](const http_exp::method &item) {
									return item == value;
								}) )
								values.push_back(value);
					if( std::ranges::none_of(values,
						[](const http_exp::method &item) {
							return item == http_exp::method(
								http_exp::method::options);
						}) )
						values.emplace_back(http_exp::method::options);
					return values;
				};

				if( target == "*" )
				{
					response_head head;
					head.headers.add("Allow", method_list(all_methods()));
					if( exchange.request().method ==
						http_exp::method(http_exp::method::options) )
						co_await exchange.respond(std::move(head),
							std::string_view{}, riwo::use_awaitable);
					else
					{
						head.status = status(uint16_t(405));
						co_await exchange.respond(std::move(head),
							std::string_view("method not allowed\n"),
							riwo::use_awaitable);
					}
					co_return;
				}

				auto selected = std::ranges::find_if(self->m_routes,
				[target](const route &value) { return value.path == target; });
				if( selected != self->m_routes.end() )
				{
					if( std::ranges::any_of(selected->methods,
					[&exchange](const http_exp::method &value) {
						return value == exchange.request().method;
					}) )
						co_await selected->handler(exchange);
					else
					{
						response_head head {status(uint16_t(405)), {}};
						head.headers.add("Allow", method_list(selected->methods));
						head.headers.add("Content-Type",
							"text/plain; charset=utf-8");
						co_await exchange.respond(std::move(head),
							std::string_view("method not allowed\n"),
							riwo::use_awaitable);
						co_return;
					}
				}
				else if( self->m_default_handler )
					co_await self->m_default_handler(exchange);
				else
				{
					response_head head {status::not_found, {}};
					head.headers.add("Content-Type",
						"text/plain; charset=utf-8");
					co_await exchange.respond(std::move(head),
						std::string_view("not found\n"),
						riwo::use_awaitable);
					co_return;
				}

				error_code ignored;
				exchange.respond({status::no_content, {}},
					body_spec::empty(), ignored);
			}
			catch(const std::exception &exception)
			{
				bool handled = false;
				try
				{
					if( self->m_service_error_handler )
						handled = self->m_service_error_handler(exchange, exception);
				}
				catch(...) {
				}
				if( not handled )
				{
						response_head head {
							status::internal_server_error, {}
						};
						head.headers.add("Content-Type",
							"text/plain; charset=utf-8");
						error_code ignored;
						exchange.respond(std::move(head),
							std::string_view("internal server error\n"),
							ignored);
				}
			}
			co_return;
		});
	}

public:
	acceptor_wrap_type m_acceptor_wrap;
	service_executor_type m_service_executor;
	config_type m_config;
	std::vector<route> m_routes;
	request_handler_type m_default_handler;
	server_error_handler_type m_server_error_handler;
	service_error_handler_type m_service_error_handler;
	std::vector<detail::server_connection_ptr> m_connections;
	bool m_running = false;
};

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_type &&acceptor,
	config_type config)
	requires core_concepts::match_sched<executor_type,service_executor_type> :
	m_impl(std::make_shared<impl>(std::move(acceptor), std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_type &&acceptor,
	ServiceExec0 &&service_executor, config_type config)
	requires core_concepts::match_sched<ServiceExec0,service_executor_type> :
	m_impl(std::make_shared<impl>(std::move(acceptor),
		get_executor_helper(std::forward<ServiceExec0>(service_executor)),
		std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_executor() noexcept -> executor_type {
	return m_impl->m_acceptor_wrap.get_executor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_service_executor() const noexcept
	-> service_executor_type {
	return m_impl->m_service_executor;
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
	auto &value = m_impl->m_acceptor_wrap.acceptor();
	if( not value.is_open() )
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
	m_impl->register_route(std::move(path),
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
	m_impl->register_route(std::move(path),
		std::vector<http_exp::method>{std::move(request_method)},
		request_handler_type(std::forward<Func>(func)));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_default(Func &&func) -> basic_server&
	requires concepts::request_handler<Func,service_executor_type>
{
	m_impl->m_default_handler = request_handler_type(std::forward<Func>(func));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_server_error(
	server_error_handler_type handler) -> basic_server&
{
	m_impl->m_server_error_handler = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_service_error(
	service_error_handler_type handler) -> basic_server&
{
	m_impl->m_service_error_handler = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_request(std::string_view path)
	-> basic_server&
{
	std::erase_if(m_impl->m_routes, [path](const typename impl::route &value) {
		return value.path == path;
	});
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_server_error() -> basic_server& {
	m_impl->m_server_error_handler = {};
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_service_error() -> basic_server& {
	m_impl->m_service_error_handler = {};
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::set_config(config_type config)
	-> basic_server&
{
	m_impl->config = std::move(config);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::config() const noexcept -> config_type {
	return m_impl->config;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t maximum) -> basic_server&
{
	error_code error;
	start(maximum, error);
	if( error ) throw std::system_error(std::error_code(error));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t maximum,
	error_code &error) noexcept -> basic_server&
{
	m_impl->start(maximum, error);
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
	size_t maximum) -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_type>
{
	m_impl->service_executor = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(maximum);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t maximum, error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_type>
{
	m_impl->service_executor = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(maximum, error);
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
auto basic_server<Exec,ServiceExec>::cancel() noexcept -> basic_server& {
	m_impl->stop();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::stop() noexcept -> basic_server& {
	m_impl->stop();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor() const noexcept
	-> const acceptor_type& {
	return m_impl->acceptor_wrap.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor() noexcept -> acceptor_type& {
	return m_impl->m_acceptor_wrap.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() const noexcept
	-> const acceptor_wrap_type& {
	return m_impl->acceptor_wrap;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() noexcept
	-> acceptor_wrap_type& {
	return m_impl->m_acceptor_wrap;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
