// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H

#include <riwo/http_exp/transport/detail/byte_stream_connection.h>
#include <riwo/http_exp/server/detail/session.h>
#include <riwo/http_exp/server/router.h>

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
		route_pattern pattern;
		std::vector<http_exp::method> methods;
		request_handler_t handler;
		std::vector<middleware_ptr_t> middleware;
	};

	struct route_table
	{
		std::vector<route> routes;
		std::vector<middleware_ptr_t> middleware;
		request_handler_t default_handler;
	};

	impl(acceptor_wrap_t &&acceptor_value,
		service_executor_t service_executor_value, config_t config_value) :
		m_acceptor_wrap(std::move(acceptor_value)),
		m_service_executor(std::move(service_executor_value)),
		m_config(std::move(config_value)),
		m_sessions(std::make_shared<application_session_manager>(
			m_config.sessions)),
		m_routes(std::make_shared<const route_table>())
	{
	}

	impl(acceptor_wrap_t &&acceptor_value, config_t config_value) :
		m_acceptor_wrap(std::move(acceptor_value)),
		m_service_executor(m_acceptor_wrap.get_executor()),
		m_config(std::move(config_value)),
		m_sessions(std::make_shared<application_session_manager>(
			m_config.sessions)),
		m_routes(std::make_shared<const route_table>())
	{
	}

	void start(size_t maximum, error_code &error) noexcept
	{
		error.clear();
		if( m_running )
			return ;
		error = detail::validate_server_config(m_config);
		if( error )
			return ;
		m_acceptor_wrap.acceptor().listen(static_cast<int>(std::min<size_t>(
			maximum, static_cast<size_t>(std::numeric_limits<int>::max()))), error);
		if( error )
			return ;
		m_running = true;
		accept_next();
	}

	void stop() noexcept
	{
		m_running = false;
		m_acceptor_wrap.cancel();
		for( auto &value : m_connections )
			if( value )
				value->cancel();
		m_connections.clear();
	}

	void register_route(std::string path,
		std::vector<http_exp::method> methods, request_handler_t handler,
		std::vector<middleware_ptr_t> middleware = {})
	{
		if( path.empty() )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: path is empty");
		if( not handler )
			throw std::invalid_argument(
				"riwo::http_exp::server::on_request: handler is empty");
		route_pattern compiled(path);
		std::lock_guard lock(m_routes_write_mutex);
		auto current = m_routes.load(std::memory_order_acquire);
		for( const auto &value : current->routes )
		{
			if( value.pattern.text() != path )
				continue;
			const bool overlaps = value.methods.empty() or methods.empty() or
				std::ranges::any_of(value.methods,
				[&methods](const http_exp::method &candidate) {
					return std::ranges::find(methods, candidate) != methods.end();
				});
			if( overlaps )
				throw std::invalid_argument(
				"riwo::http_exp::server::on_request: route/method is already bound");
		}
		auto updated = std::make_shared<route_table>(*current);
		updated->routes.push_back(route {std::move(compiled),
			std::move(methods), std::move(handler), std::move(middleware)});
		m_routes.store(std::shared_ptr<const route_table>(std::move(updated)),
			std::memory_order_release);
	}

	void use_middleware(middleware_ptr_t middleware)
	{
		if( not middleware )
			throw std::invalid_argument(
			"riwo::http_exp::server::use: middleware is null");
		std::lock_guard lock(m_routes_write_mutex);
		auto current = m_routes.load(std::memory_order_acquire);
		auto updated = std::make_shared<route_table>(*current);
		updated->middleware.emplace_back(std::move(middleware));
		m_routes.store(std::shared_ptr<const route_table>(std::move(updated)),
			std::memory_order_release);
	}

	void report_server_error(error_code error) noexcept
	{
		if( error == asio::error::make_error_code(asio::error::operation_aborted) )
			return ;
		try {
			if( m_server_error_handler )
				(void)m_server_error_handler(error);
		}
		catch(...) {
		}
	}

private:
	[[nodiscard]] static std::string method_list(
		std::vector<http_exp::method> values)
	{
		if( std::ranges::any_of(values, [](const http_exp::method &value) {
			return value == http_exp::method(http_exp::method::get);
		}) and std::ranges::none_of(values, [](const http_exp::method &value) {
			return value == http_exp::method(http_exp::method::head);
		}) )
			values.emplace_back(http_exp::method::head);
		if( std::ranges::none_of(values, [](const http_exp::method &value) {
			return value == http_exp::method(http_exp::method::options);
		}) )
			values.emplace_back(http_exp::method::options);
		std::string result;
		for( const auto &value : values )
		{
			if( not result.empty() )
				result += ", ";
			result += value.text();
		}
		return result;
	}

	[[nodiscard]] static bool method_matches(const route &value,
		const http_exp::method &request_method, bool allow_head_fallback) noexcept
	{
		if( value.methods.empty() )
			return true;
		if( std::ranges::find(value.methods, request_method) !=
			value.methods.end() )
			return true;
		return allow_head_fallback and
			request_method == http_exp::method(http_exp::method::head) and
			std::ranges::find(value.methods,
				http_exp::method(http_exp::method::get)) != value.methods.end();
	}

	[[nodiscard]] static awaitable<void> call_route(context_t &exchange,
		const route &selected, const route_table &table)
	{
		std::vector<middleware_ptr_t> chain = table.middleware;
		chain.insert(chain.end(), selected.middleware.begin(),
			selected.middleware.end());
		size_t entered = 0;
		try
		{
			bool stopped = false;
			for( ; entered < chain.size(); ++entered )
			{
				const auto &middleware = chain[entered];
				if( middleware and co_await middleware->before(exchange) )
				{
					++entered;
					stopped = true;
					break;
				}
			}
			if( not stopped )
				co_await selected.handler(exchange);
			while( entered != 0 )
			{
				const auto &middleware = chain[--entered];
				if( middleware )
					(void)co_await middleware->after(exchange);
			}
		}
		catch(const std::exception &exception)
		{
			while( entered != 0 )
			{
				const auto &middleware = chain[--entered];
				try
				{
					if( middleware and middleware->exception(exchange, exception) )
						co_return ;
				}
				catch(...) {
				}
			}
			throw;
		}
		co_return ;
	}

	[[nodiscard]] awaitable<void> fallback(context_t &exchange,
		http_exp::status response_status,
		const std::shared_ptr<const route_table> &routes)
	{
		if( exchange.response_started() )
			co_return ;
		if( routes->default_handler )
		{
			co_await routes->default_handler(exchange);
			if( exchange.response_started() )
				co_return ;
		}
		if( not m_config.default_responses.enabled )
		{
			co_await exchange.respond({status::no_content, {}},
				body_spec::empty(), riwo::use_awaitable);
			co_return ;
		}

		response_head head {response_status, {}};
		std::string body;
		if( m_config.default_responses.html )
		{
			head.headers.add("Content-Type", "text/html; charset=utf-8");
			const auto title = response_status == status::ok ?
				std::string("Welcome to RIWO") : std::string("RIWO");
			const auto detail = response_status == status::ok ? std::string{} :
				std::format("<h2>{} ({})</h2>", response_status.text(),
					response_status.code());
			body = std::format(
				"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
				"<title>{}</title></head><body><h1>{}</h1>{}"
				"<p>[ This is the server's default reply ]</p>"
				"</body></html>", title, title, detail);
		}
		else
		{
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			body = response_status == status::ok ? "Welcome to RIWO\n" :
				std::format("{} ({})\n", response_status.text(),
					response_status.code());
		}
		co_await exchange.respond(std::move(head), riwo::buffer(body),
			riwo::use_awaitable);
	}

	[[nodiscard]] awaitable<void> method_not_allowed(context_t &exchange,
		std::vector<http_exp::method> allowed,
		const std::shared_ptr<const route_table> &routes)
	{
		response_head head {status::method_not_allowed, {}};
		head.headers.add("Allow", method_list(std::move(allowed)));
		if( routes->default_handler )
		{
			co_await routes->default_handler(exchange);
			if( exchange.response_started() )
				co_return ;
		}
		if( not m_config.default_responses.enabled )
		{
			head.status = status::no_content;
			co_await exchange.respond(std::move(head), body_spec::empty(),
				riwo::use_awaitable);
			co_return ;
		}
		std::string body = m_config.default_responses.html ?
			std::format("<!DOCTYPE html><html><body><h1>{} ({})</h1>"
				"</body></html>", head.status.text(), head.status.code()) :
			std::format("{} ({})\n", head.status.text(), head.status.code());
		head.headers.add("Content-Type", m_config.default_responses.html ?
			"text/html; charset=utf-8" : "text/plain; charset=utf-8");
		co_await exchange.respond(std::move(head), riwo::buffer(body),
			riwo::use_awaitable);
	}

	void accept_next()
	{
		if( not m_running )
			return ;
		auto self = this->shared_from_this();
		m_acceptor_wrap.accept(
		[self](error_code error, typename acceptor_wrap_t::accepted_t accepted)
			mutable
		{
			if( not self->m_running )
				return ;
			self->accept_next();
			if( error )
			{
				self->report_server_error(error);
				return ;
			}
			if( auto *transport = std::get_if<
				typename acceptor_wrap_t::transport_t>(&accepted) )
			{
				error_code serve_error;
				self->serve_transport(std::move(*transport), serve_error);
				if( serve_error )
					self->report_server_error(serve_error);
				return ;
			}
			auto *socket = std::get_if<typename acceptor_wrap_t::socket_t>(
				&accepted);
			if( not socket )
			{
				self->report_server_error(make_error_code(
					errc::negotiation_failed));
				return ;
			}
			auto connection = detail::accept_server(std::move(*socket),
				self->m_config,
			[self]
			(error_code accept_error, std::shared_ptr<detail::stream> stream,
				request_head request) mutable {
				if( accept_error )
				{
					self->report_server_error(accept_error);
					return ;
				}
				self->serve(std::move(stream), std::move(request));
			});
			std::erase_if(self->m_connections,
				[](const detail::server_connection_ptr_t &value) {
					return not value or value->closed();
				});
			self->m_connections.push_back(std::move(connection));
		});
	}

public:
	void serve_transport(basic_negotiated_transport<executor_t> transport,
		error_code &error) noexcept
	{
		error.clear();
		try
		{
			const auto selected = transport.selected_protocol;
			auto connection = detail::make_connection(std::move(transport));
			auto self = this->shared_from_this();
			auto active = detail::accept_server(std::move(connection), selected,
				m_config,
			[self](error_code accept_error,
				std::shared_ptr<detail::stream> stream,
				request_head request) mutable
			{
				if( accept_error )
				{
					self->report_server_error(accept_error);
					return ;
				}
				self->serve(std::move(stream), std::move(request));
			});
			std::erase_if(m_connections,
				[](const detail::server_connection_ptr_t &value) {
					return not value or value->closed();
				});
			if( active )
				m_connections.push_back(std::move(active));
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
	}

private:

	void serve(std::shared_ptr<detail::stream> stream, request_head request)
	{
		auto self = this->shared_from_this();
		riwo::dispatch(m_service_executor,
		[self = std::move(self), stream = std::move(stream),
		 request = std::move(request)]
		() mutable -> awaitable<void>
		{
			auto target_storage = request.target;
			auto target = std::string_view(target_storage);
			auto query_text = std::string_view{};
			if( const auto query = target.find('?');
				query != std::string_view::npos )
			{
				query_text = target.substr(query + 1);
				target = target.substr(0, query);
			}
			auto query_parameters = parse_query_parameters(query_text);
			if( not query_parameters )
				query_parameters = parameter_view{};
			auto routes = self->m_routes.load(std::memory_order_acquire);

			const route *selected = nullptr;
			optional<route_match> selected_match;
			bool selected_direct = false;
			size_t best_specificity = 0;
			std::vector<const route*> path_matches;
			for( const auto &candidate : routes->routes )
			{
				auto match = candidate.pattern.match(target);
				if( not match )
					continue;
				if( path_matches.empty() or
					match->specificity > best_specificity )
				{
					best_specificity = match->specificity;
					path_matches.clear();
					selected = nullptr;
					selected_match.reset();
					selected_direct = false;
				}
				if( match->specificity != best_specificity )
					continue;
				path_matches.push_back(&candidate);
				// Prefer a direct method match over the implicit HEAD -> GET rule.
				if( method_matches(candidate, request.method, false) )
				{
					selected = &candidate;
					selected_match = std::move(match);
					selected_direct = true;
				}
				else if( not selected_direct and not selected and method_matches(candidate,
					request.method, true) )
				{
					selected = &candidate;
					selected_match = std::move(match);
				}
			}

			context_t exchange(self->m_service_executor, stream,
				std::move(request), selected_match ?
					std::move(selected_match->path_parameters) : parameter_view{},
				std::move(*query_parameters), self->m_sessions,
				self->m_config.resource_root,
				self->m_config.compression.enabled,
				self->m_config.compression.minimum_size);
			bool service_failed = false;
			try
			{
				auto all_methods = [routes]() {
					std::vector<http_exp::method> values;
					for( const auto &route : routes->routes )
						for( const auto &value : route.methods )
							if( std::ranges::none_of(values,
								[&value](const http_exp::method &item) {
									return item == value;
								}) )
								values.push_back(value);
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
						co_await self->method_not_allowed(exchange,
							all_methods(), routes);
					co_return ;
				}

				if( selected )
				{
					co_await call_route(exchange, *selected, *routes);
					co_await self->fallback(exchange, status::ok, routes);
				}
				else if( not path_matches.empty() )
				{
					std::vector<http_exp::method> allowed;
					for( const auto *candidate : path_matches )
						for( const auto &value : candidate->methods )
							if( std::ranges::find(allowed, value) == allowed.end() )
								allowed.push_back(value);
					co_await self->method_not_allowed(exchange,
						std::move(allowed), routes);
				}
				else
					co_await self->fallback(exchange, status::not_found, routes);
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
				ignore_unused(handled);
				service_failed = true;
			}
			if( service_failed and not exchange.response_started() )
			{
				try { co_await self->fallback(exchange,
					status::internal_server_error, routes); }
				catch(...) {
					exchange.cancel();
				}
			}
			co_return ;
		});
	}

public:
	acceptor_wrap_t m_acceptor_wrap;
	service_executor_t m_service_executor;
	config_t m_config;
	std::shared_ptr<application_session_manager> m_sessions;
	std::atomic<std::shared_ptr<const route_table>> m_routes;
	std::mutex m_routes_write_mutex;
	server_error_handler_t m_server_error_handler;
	service_error_handler_t m_service_error_handler;
	std::vector<detail::server_connection_ptr_t> m_connections;
	bool m_running = false;
};

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(basic_server &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	auto executor = m_impl->m_acceptor_wrap.get_executor();
	other.m_impl = std::make_shared<impl>(
		acceptor_wrap_t(acceptor_t(std::move(executor))),
		m_impl->m_service_executor, config_t{});
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::operator=(basic_server &&other) noexcept
	-> basic_server&
{
	if( this == &other )
		return *this;
	auto executor = other.m_impl->m_acceptor_wrap.get_executor();
	auto replacement = std::make_shared<impl>(
		acceptor_wrap_t(acceptor_t(std::move(executor))),
		other.m_impl->m_service_executor, config_t{});
	m_impl = std::move(other.m_impl);
	other.m_impl = std::move(replacement);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(config_t config)
	requires (
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>) :
	basic_server(executor_t(riwo::get_executor()), std::move(config))
{
}

#if RIWO_OPENSSL_SUPPORT

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(
	std::shared_ptr<server_tls_context> tls_context, config_t config)
	requires (
		std::same_as<executor_t,asio::any_io_executor> and
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>) :
	basic_server(make_tls_acceptor(acceptor_wrap_t(), std::move(tls_context)),
		std::move(config))
{
}

#endif

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Exec0>
basic_server<Exec,ServiceExec>::basic_server(Exec0 &&executor, config_t config)
	requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_server> and
		core_concepts::match_sched<Exec0,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>) :
	basic_server(acceptor_wrap_t(acceptor_t(
		get_executor_helper(std::forward<Exec0>(executor)))), std::move(config))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_t &&acceptor,
	config_t config)
	requires core_concepts::match_sched<executor_t,service_executor_t> :
	m_impl(std::make_shared<impl>(std::move(acceptor), std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
basic_server<Exec,ServiceExec>::basic_server(acceptor_wrap_t &&acceptor,
	ServiceExec0 &&service_executor, config_t config)
	requires core_concepts::match_sched<ServiceExec0,service_executor_t> :
	m_impl(std::make_shared<impl>(std::move(acceptor),
		get_executor_helper(std::forward<ServiceExec0>(service_executor)),
		std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_executor() noexcept -> executor_t {
	return m_impl->m_acceptor_wrap.get_executor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_service_executor() const noexcept
	-> service_executor_t {
	return m_impl->m_service_executor;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_t endpoint)
	-> basic_server&
{
	error_code error;
	bind(std::move(endpoint), error);
	if( error )
		throw std::system_error(std::error_code(error));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_t endpoint,
	error_code &error) noexcept -> basic_server&
{
	auto &value = m_impl->m_acceptor_wrap.acceptor();
	if( not value.is_open() )
	{
		value.open(endpoint->protocol(), error);
		if( error )
			return *this;
	}
	value.set_option(asio::socket_base::reuse_address(true), error);
	if( error )
		return *this;
	value.bind(*endpoint, error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <method::well_known... Methods, typename Func, typename... Middleware>
auto basic_server<Exec,ServiceExec>::on_request(std::string path, Func &&func,
	Middleware&&... middleware)
	-> basic_server&
	requires concepts::request_handler<Func,service_executor_t> and
		concepts::middleware_list<service_executor_t,Middleware...>
{
	m_impl->register_route(std::move(path),
		std::vector<http_exp::method>{http_exp::method(Methods)...},
		request_handler_t(std::forward<Func>(func)),
		std::vector<middleware_ptr_t> {
			middleware_ptr_t(std::forward<Middleware>(middleware))...
		});
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func, typename... Middleware>
auto basic_server<Exec,ServiceExec>::on_request(http_exp::method request_method,
	std::string path, Func &&func, Middleware&&... middleware) -> basic_server&
	requires concepts::request_handler<Func,service_executor_t> and
		concepts::middleware_list<service_executor_t,Middleware...>
{
	m_impl->register_route(std::move(path),
		std::vector<http_exp::method>{std::move(request_method)},
		request_handler_t(std::forward<Func>(func)),
		std::vector<middleware_ptr_t> {
			middleware_ptr_t(std::forward<Middleware>(middleware))...
		});
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <method::well_known... Methods>
auto basic_server<Exec,ServiceExec>::on_request(std::string path,
	controller_ptr_t controller) -> basic_server&
{
	if( not controller )
		throw std::invalid_argument(
		"riwo::http_exp::server::on_request: controller is null");
	auto handler = [controller](context_t &exchange) -> awaitable<void> {
		co_await controller->service(exchange);
	};
	return on_request<Methods...>(std::move(path), std::move(handler),
		std::move(controller));
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::use(middleware_ptr_t middleware)
	-> basic_server&
{
	m_impl->use_middleware(std::move(middleware));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_default(Func &&func) -> basic_server&
	requires concepts::request_handler<Func,service_executor_t>
{
	std::lock_guard lock(m_impl->m_routes_write_mutex);
	auto current = m_impl->m_routes.load(std::memory_order_acquire);
	auto updated = std::make_shared<typename impl::route_table>(*current);
	updated->default_handler = request_handler_t(std::forward<Func>(func));
	m_impl->m_routes.store(
		std::shared_ptr<const typename impl::route_table>(std::move(updated)),
		std::memory_order_release);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_server_error(
	server_error_handler_t handler) -> basic_server&
{
	m_impl->m_server_error_handler = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_service_error(
	service_error_handler_t handler) -> basic_server&
{
	m_impl->m_service_error_handler = std::move(handler);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_request(std::string_view path)
	-> basic_server&
{
	std::lock_guard lock(m_impl->m_routes_write_mutex);
	auto current = m_impl->m_routes.load(std::memory_order_acquire);
	auto updated = std::make_shared<typename impl::route_table>(*current);
	std::erase_if(updated->routes,
	[path](const typename impl::route &value) {
		return value.pattern.text() == path;
	});
	m_impl->m_routes.store(
		std::shared_ptr<const typename impl::route_table>(std::move(updated)),
		std::memory_order_release);
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
auto basic_server<Exec,ServiceExec>::set_config(config_t config)
	-> basic_server&
{
	m_impl->m_config = std::move(config);
	m_impl->m_sessions->set_config(m_impl->m_config.sessions);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::config() const noexcept -> config_t {
	return m_impl->m_config;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t maximum) -> basic_server&
{
	error_code error;
	start(maximum, error);
	if( error )
		throw std::system_error(std::error_code(error));
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
auto basic_server<Exec,ServiceExec>::serve(
	basic_negotiated_transport<executor_t> transport) -> basic_server&
{
	error_code error;
	serve(std::move(transport), error);
	if( error )
		throw std::system_error(std::error_code(error));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::serve(
	basic_negotiated_transport<executor_t> transport,
	error_code &error) noexcept -> basic_server&
{
	m_impl->serve_transport(std::move(transport), error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t maximum) -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
{
	m_impl->m_service_executor = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(maximum);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t maximum, error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
{
	m_impl->m_service_executor = get_executor_helper(
		std::forward<ServiceExec0>(service_executor));
	return start(maximum, error);
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
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
	-> const acceptor_t& {
	return m_impl->m_acceptor_wrap.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor() noexcept -> acceptor_t& {
	return m_impl->m_acceptor_wrap.acceptor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() const noexcept
	-> const acceptor_wrap_t& {
	return m_impl->m_acceptor_wrap;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::acceptor_wrap() noexcept
	-> acceptor_wrap_t& {
	return m_impl->m_acceptor_wrap;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_SERVER_H
