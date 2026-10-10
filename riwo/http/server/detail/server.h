// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_SERVER_H
#define RIWO_HTTP_SERVER_DETAIL_SERVER_H

namespace riwo::http
{

template <concepts::any_exec_stream Stream>
class RIWO_HTTP_TAPI basic_server<Stream>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)
	using request_handler_t = std::function<awaitable<void>(context_t&)>;
	using connection_ptr = std::shared_ptr<connection_t>;

public:
	impl(acceptor_wrap_t &&wrap, asio::any_io_executor service_exec) :
		m_wrap(std::move(wrap)), m_service_exec(std::move(service_exec))
	{
		bind_session_error_handler();
	}

	explicit impl(acceptor_wrap_t &&wrap) :
		m_wrap(std::move(wrap))
	{
		m_service_exec = m_wrap.acceptor().get_executor();
		bind_session_error_handler();
	}

public:
	void async_start(size_t max, error_code &error) noexcept {
		async_start(m_service_exec, max, error);
	}

	void async_start(const executor_t &service_exec, size_t max, error_code &error) noexcept
	{
		error.clear();
		if( m_is_start )
			return ;

		m_wrap.acceptor().listen(static_cast<int>(max), error);
		if( error )
			return ;
		m_is_start = true;

		riwo::dispatch(m_wrap.acceptor().get_executor(),
		[self = this->shared_from_this(), service_exec]() mutable
		{
			bool abd = false;
			try {
				self->do_tcp_accept(service_exec);
			}
			catch(...) {
				abd = true;
			}
			if( abd )
			{
				error_code ignored {};
				self->m_wrap.acceptor().cancel(ignored);
				self->m_wrap.acceptor().close(ignored);
				self->m_is_start = false;
				forced_termination();
			}
		});
	}

	void bind_session_error_handler()
	{
		m_session_manager.on_error (
		[this](const session_ptr&, const error_code &error)
		{
			call_on_server_error(error);
		});
	}

	void set_config(config_t config) noexcept
	{
		using namespace std::chrono_literals;
		if( config.first_reading_time <= 0ms )
			config.first_reading_time = 1ms;

		if( config.keepalive_time < 0ms )
			config.keepalive_time = 0ms;

		if constexpr( requires { config.tls_handshake_timeout; } )
		{
			if( config.tls_handshake_timeout <= 0ms )
				config.tls_handshake_timeout = 1ms;
		}
		m_config = std::move(config);
	}

	[[nodiscard]] config_t config() const noexcept
	{
		return m_config;
	}

	void rule_path_check(std::string &str)
	{
		auto n_it = std::unique(str.begin(), str.end(), [](char c0, char c1){
			return c0 == c1 and c0 == 0x2F/*/*/;
		});
		if( n_it != str.end() )
			str.erase(n_it, str.end());

		if( not str.starts_with('/') )
			str = "/" + str;

		if( str.size() > 1 and str.ends_with('/') )
			str.pop_back();
	}

private:
	void do_tcp_accept(const executor_t &service_exec)
	{
		auto callback =
		[server_self = this->shared_from_this(), service_exec](connection_ptr accepted_connection) mutable
		{
			if( not accepted_connection )
			{
				if( not server_self->m_is_start )
				{
					error_code ignored {};
					server_self->m_wrap.acceptor().cancel(ignored);
					server_self->m_wrap.acceptor().close(ignored);
				}
				return ;
			}
			if( not server_self->m_is_start or not accepted_connection->is_open() )
			{
				ignore_unused(accepted_connection->close());
				return ;
			}
			tcp_socket_options options {};
			options.no_delay = true;

			auto set_result = accepted_connection->set_options(options);
			if( not set_result )
			{
				ignore_unused(accepted_connection->close());
				server_self->call_on_server_error(set_result.error());
				return ;
			}
			auto config = server_self->m_config;
			riwo::dispatch(service_exec,
			[server = std::move(server_self), client_connection = std::move(accepted_connection), config]
			() mutable -> awaitable<void>
			{
				bool abd = false;
				bool released = false;
				try {
					released = co_await server->do_tcp_service (
						client_connection, config.first_reading_time,
						config.keepalive_time, config.resource_root
					);
				}
				catch(...) {
					abd = true;
				}
				if( not released )
					ignore_unused(client_connection->close());
				if( abd )
					forced_termination();
				co_return ;
			});
		};

		if constexpr( requires { m_config.tls_handshake_timeout; } )
		{
			m_wrap.accept(service_exec, std::move(callback),
				m_config.tls_handshake_timeout
			);
		}
		else
			m_wrap.accept(service_exec, std::move(callback));
	}

	[[nodiscard]] awaitable<bool> do_tcp_service
	(const connection_ptr &client_connection, milliseconds first_reading_time,
		milliseconds keepalive_time, std::filesystem::path resource_root)
	{
		using namespace std::chrono_literals;
		using namespace riwo::operators;

		const auto *time = &first_reading_time;
		std::string pending_data {};
		for(;;)
		{
			server_parser parser {};
			if( not pending_data.empty() )
			{
				if( auto expected = parser.append(buffer(pending_data)); not expected )
				{
					call_on_server_error(expected.error());
					break;
				}
			}
			context_t context (
				client_connection, std::move(parser), m_session_manager,
				resource_root
			);
			try {
				co_await context.request().wait(use_awaitable | *time);
			}
			catch(std::system_error &ex)
			{
				auto eno = ex.code().value();
				if( eno == errc::bad_descriptor or eno == errc::eof or
					eno == errc::timed_out )
					break;
				call_on_server_error(ex.code());
				break;
			}
			context.response().auto_set(context.request());
			if( auto expectation = context.request().header(header::expect); expectation )
			{
				auto value = strtls::to_lower(strtls::trimmed(**expectation));
				if( context.request().version() < version::v11 or value != "100-continue" )
				{
					context.response()
						.set_status(status::expectation_failed)
						.set_header(header::connection, "close");

					co_await context.response().write({nullptr,0}, use_awaitable);
					break;
				}
			}
			co_await call_on_request(context);

			// An upgrade handler may hand the transport over even when it does not
			// finish an HTTP response (for example, when a timed-out handshake has
			// already closed the transport). Never run the default HTTP responder on
			// a connection that has left the HTTP request lifecycle.
			if( context.connection_handed_over() )
				co_return true;

			if( not context.response().is_finished() )
				co_await call_on_default(context);

			if( context.connection_handed_over() )
				co_return true;

			if( not context.request().keep_alive() )
				break;

			if( context.request().can_read_body() )
			{
				if( context.request().header(header::expect) )
					break;
				try
				{
					ignore_unused(
						co_await context.request().read(use_awaitable)
					);
				}
				catch(const std::system_error &ex)
				{
					call_on_server_error(ex.code());
					break;
				}
			}
			pending_data = context.request().take_pending_data();

			time = &keepalive_time;
			if( *time == 0ms )
				break;
		}
		co_return false;
	}

private:
	[[nodiscard]] awaitable<void> call_on_request(context_t &context)
	{
		tk_handler_ptr handler {};
		const std::string *selected_rule = nullptr;
		auto routes = load_routes();

		if( auto exact = routes->exact.find(context.request().path());
			exact != routes->exact.end() )
			handler = exact->second;
		else
		{
			int32_t weight = std::numeric_limits<int32_t>::max();
			size_t path_length = std::numeric_limits<size_t>::min();
			auto path_index = index_request_path(context.request().path());

			for(const auto &[rule, route] : routes->patterns)
			{
				auto _path_length = rule.length();
				auto _weight = match_pattern(route, path_index);

				if( _weight == 0 )
				{
					handler = route.handler;
					selected_rule = &rule;
					weight = _weight;
					break;
				}
				else if( _weight > 0 and (
					_weight < weight or (_weight == weight and _path_length > path_length) ) )
				{
					handler = route.handler;
					selected_rule = &rule;
					weight = _weight;
					path_length = _path_length;
				}
			}
		}
		if( not handler )
		{
			context.response().set_status(status::not_found);
			co_return ;
		}
		if( selected_rule )
			ignore_unused(context.request().path_match(*selected_rule));

		auto method = context.request().method();
		if( not ( handler->method & method ) )
		{
			if( method == method::head )
			{
				co_await context.response()
					.set_header(header::content_type,"text/plain")
					.write(use_awaitable);
			}
			else if( method == method::options )
			{
				auto body = options_response_body(handler->method);
				co_await context.response()
					.set_header(header::content_type,"text/plain")
					.write(body, use_awaitable);
			}
			else
			{
				context.response().set_status (
					status::method_not_allowed
				);
			}
			co_return ;
		}
		try
		{
			if( co_await handler->aop->before(context) )
				co_return ;

			co_await handler->aop->service(context);
			co_await handler->aop->after(context);
		}
		catch(const std::exception &ex)
		{
			if( handler->aop->exception(context, ex) )
				co_return ;
			call_on_service_error(context, ex);
		}
		co_return ;
	}

	static constexpr auto def_html_v =
		"<!DOCTYPE html>"
		"<html>"
		"<head>"
		"	<meta charset=\"utf-8\">"
		"	<title>{0}</title>"
		"</head>"
		"<body>"
		"	<h1>{0}</h1>{1}"
		"	<p>[ This is the server's default reply ]</p>"
		"	<p>-----------------------------------------------</p>"
		"	<p>This is an open source C++ (ASIO) server.</p>"
		"	<a href=\"https://gitee.com/jin-xiaoqiang/riwo\" target=\"_blank\">"
		"		Source code repository (Gitee)"
		"	</a>"
		"</body>"
		"</html>";

	[[nodiscard]] awaitable<void> call_on_default(context_t &context)
	{
		try {
			if( m_default_handler )
			{
				co_await m_default_handler(context);
				if( context.response().is_finished() )
					co_return ;
			}
			std::string data {};
			if( context.response().status() == status::ok )
				data = std::format(def_html_v, "Welcome to RIWO", "");
			else
			{
				auto status = std::format (
					"<h2>{} ({})</h2>",
					status::description(context.response().status()),
					context.response().status()
				);
				data = std::format(def_html_v, "RIWO", status);
			}
			co_await context.response()
				.set_header(header::content_type, "text/html")
				.write(data, use_awaitable);
		}
		catch(const std::exception &ex) {
			call_on_service_error(context, ex);
		}
		co_return ;
	}

private:
	void call_on_server_error(const error_code &error)
	{
		if( not m_server_error_handler or not m_server_error_handler(error) )
			system_error::loc_throw(error, "riwo::http::server");
	}

	void call_on_service_error(context_t &context, const std::exception &ex)
	{
		context.response().set_status(status::internal_server_error);
		if( m_service_error_handler and m_service_error_handler(context, ex) )
			return ;
		throw ;
	}

	[[nodiscard]] static std::string options_response_body(methods method)
	{
		std::string sum {};
		for(uint16_t i=method::get; i<=method::connect; i=static_cast<uint16_t>(i<<1))
		{
			if( not ( method & i ) )
				continue;

			sum += std::format("{};", method::string (
				static_cast<method_enum>(i)
			));
		}
		if( not sum.empty() )
			sum.pop_back();
		return sum;
	}

public:
	class multi_ctrlr_aop : public ctrlr_aop_t
	{
	public:
		template <typename Func, typename...AopPtrs>
		explicit multi_ctrlr_aop(Func &&func, AopPtrs&&...aops) :
			m_aops{aop_ptr_t(std::forward<AopPtrs>(aops))...},
			m_func(std::forward<Func>(func))
		{
			assert(m_func);
		}

	public:
		[[nodiscard]] awaitable<bool> before(context_t &context) override
		{
			for(auto &interceptor : m_aops)
			{
				if( co_await interceptor->before(context) )
					co_return true;
			}
			co_return false;
		}

		[[nodiscard]] awaitable<bool> after(context_t &context) override
		{
			for(auto &interceptor : m_aops)
			{
				if( co_await interceptor->after(context) )
					co_return true;
			}
			co_return false;
		}

		[[nodiscard]] bool exception(context_t &context, const std::exception &ex) override
		{
			for(auto &interceptor : m_aops)
			{
				if( interceptor->exception(context, ex) )
					return true;
			}
			return false;
		}

	public:
		[[nodiscard]] awaitable<void> service(context_t &context) override {
			co_return co_await m_func(context);
		}

	private:
		std::vector<aop_ptr_t> m_aops {};
		request_handler_t m_func {};
	};

	struct tk_handler
	{
		explicit tk_handler(ctrlr_aop_ptr_t handler_aop) :
			aop(std::move(handler_aop)) {}

		template <method_enum...Method>
		tk_handler &bind_method()
		{
			if constexpr( sizeof...(Method) == 0 )
			{
#define X_MACRO(e,v,d) method |= method_enum::e;
				RIWO_HTTP_METHOD_TABLE
#undef X_MACRO
			}
			else
			{
				(void) std::initializer_list<int> {
					(method |= Method, 0) ...
				};
			}
			return *this;
		}
		methods method {};
		ctrlr_aop_ptr_t aop {};
	};
	using tk_handler_ptr = std::shared_ptr<tk_handler>;

	struct transparent_string_hash
	{
		using is_transparent = void;

		[[nodiscard]] size_t operator()(std::string_view value) const noexcept {
			return std::hash<std::string_view>{}(value);
		}
		[[nodiscard]] size_t operator()(const std::string &value) const noexcept {
			return operator()(std::string_view(value));
		}
	};
	using exact_route_map = std::unordered_map <
		std::string, tk_handler_ptr, transparent_string_hash, std::equal_to<>
	>;

	struct pattern_route
	{
		tk_handler_ptr handler {};
		std::string wildcard_rule {};
		std::vector<std::string> argument_names {};

		size_t segment_count = 0;
		size_t wildcard_weight = 0;
	};

	struct request_path_index
	{
		std::string normalized {};
		std::vector<size_t> segment_ends {};
	};

	struct route_table
	{
		exact_route_map exact {};
		std::map<std::string,pattern_route> patterns {};
	};

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
	using route_table_snapshot = std::atomic<std::shared_ptr<const route_table>>;
#else //clang-libc++
	using route_table_snapshot = std::shared_ptr<const route_table>;
#endif //clang-libc++

private:
	[[nodiscard]] std::shared_ptr<const route_table> load_routes() const noexcept
	{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
		return m_routes.load(std::memory_order_acquire);
#else //clang-libc++
		return std::atomic_load_explicit(&m_routes, std::memory_order_acquire);
#endif //clang-libc++
	}

	void store_routes(std::shared_ptr<const route_table> routes) noexcept
	{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
		m_routes.store(std::move(routes), std::memory_order_release);
#else //clang-libc++
		std::atomic_store_explicit(&m_routes, std::move(routes), std::memory_order_release);
#endif //clang-libc++
	}

	[[nodiscard]] static bool is_path_argument_segment(std::string_view segment) noexcept
	{
		if( segment.size() < 2 or not segment.starts_with('{') or
			not segment.ends_with('}') )
			return false;

		return segment.substr(1, segment.size() - 2).find_first_of("{}") ==
			std::string_view::npos;
	}

	[[nodiscard]] static bool is_exact_route(std::string_view rule) noexcept
	{
		if( rule.find_first_of("*?") != std::string_view::npos )
			return false;

		auto slash = rule.rfind('/');
		auto segment = slash == std::string_view::npos ?
			rule : rule.substr(slash + 1);

		return not is_path_argument_segment(segment);
	}

	[[nodiscard]] static bool is_blank_segment(std::string_view segment) noexcept
	{
		return std::ranges::all_of(segment, [](unsigned char ch) {
			return ch >= 1 and ch <= 32;
		});
	}

	[[nodiscard]] static std::vector<std::string_view> split_path(std::string_view path)
	{
		std::vector<std::string_view> result;
		result.reserve(path.size() / 2 + 1);

		size_t begin = 0;
		for(;;)
		{
			auto end = path.find('/', begin);
			if( end == std::string_view::npos )
				end = path.size();

			if( auto segment = path.substr(begin, end - begin);
				not segment.empty() and not is_blank_segment(segment) )
				result.emplace_back(segment);

			if( end == path.size() )
				break;
			begin = end + 1;
		}
		if( result.empty() )
			result.emplace_back("/");
		return result;
	}

	[[nodiscard]] static pattern_route compile_pattern(std::string_view rule, tk_handler_ptr handler)
	{
		auto segments = split_path(rule);
		pattern_route result {};

		result.handler = std::move(handler);
		result.segment_count = segments.size();

		size_t prefix_count = segments.size();
		while( prefix_count > 0 and is_path_argument_segment(segments[prefix_count - 1]) )
			--prefix_count;

		result.argument_names.reserve(segments.size() - prefix_count);
		for(size_t index=prefix_count; index<segments.size(); ++index)
		{
			result.argument_names.emplace_back (
				segments[index].substr(1, segments[index].size() - 2)
			);
		}
		for(size_t index=0; index<prefix_count; ++index)
		{
			if( index != 0 )
				result.wildcard_rule.push_back('/');
			result.wildcard_rule.append(segments[index]);
		}
		for(auto ch : result.wildcard_rule)
		{
			if( ch == '?' )
				++result.wildcard_weight;
			else if( ch == '*' )
				result.wildcard_weight += 2;
		}
		return result;
	}

	[[nodiscard]] static request_path_index index_request_path(std::string_view path)
	{
		auto segments = split_path(path);
		request_path_index result {};

		result.normalized.reserve(path.size());
		result.segment_ends.reserve(segments.size());

		for(size_t index=0; index<segments.size(); ++index)
		{
			if( index != 0 )
				result.normalized.push_back('/');

			result.normalized.append(segments[index]);
			result.segment_ends.emplace_back(result.normalized.size());
		}
		return result;
	}

	[[nodiscard]] static bool wildcard_matches(std::string_view rule, std::string_view path) noexcept
	{
		size_t rule_pos = 0;
		size_t path_pos = 0;

		size_t star_pos = std::string_view::npos;
		size_t star_path_pos = 0;

		while( path_pos < path.size() )
		{
			if( rule_pos < rule.size() and
				(rule[rule_pos] == '?' or rule[rule_pos] == path[path_pos]) )
			{
				++rule_pos;
				++path_pos;
			}
			else if( rule_pos < rule.size() and rule[rule_pos] == '*' )
			{
				star_pos = rule_pos++;
				star_path_pos = path_pos;
			}
			else if( star_pos != std::string_view::npos )
			{
				rule_pos = star_pos + 1;
				path_pos = ++star_path_pos;
			}
			else
				return false;
		}
		while( rule_pos < rule.size() and rule[rule_pos] == '*' )
			++rule_pos;
		return rule_pos == rule.size();
	}

	[[nodiscard]] static int32_t match_pattern
	(const pattern_route &route, const request_path_index &path) noexcept
	{
		if( path.segment_ends.size() < route.segment_count )
			return -1;

		const auto prefix_count = path.segment_ends.size() - route.argument_names.size();
		const auto prefix_size = prefix_count == 0 ? 0 : path.segment_ends[prefix_count - 1];

		const std::string_view prefix(path.normalized.data(), prefix_size);
		if( not wildcard_matches(route.wildcard_rule, prefix) )
			return -1;

		const auto weight = route.wildcard_weight > 0 and
			prefix.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()) / route.wildcard_weight ?
			std::numeric_limits<int32_t>::max() : static_cast<int32_t>(prefix.size() * route.wildcard_weight);
		return weight;
	}

public:
	[[nodiscard]] bool add_route(std::string rule, tk_handler_ptr handler)
	{
		// Publishing a table copies containers and may allocate. A blocking writer
		// lock avoids wasting CPU; request dispatch never waits on this mutex.
		std::lock_guard lock(m_routes_write_mutex);
		auto current = load_routes();

		if( current->exact.contains(rule) or current->patterns.contains(rule) )
			return false;

		auto updated = std::make_shared<route_table>(*current);
		if( is_exact_route(rule) )
			updated->exact.emplace(std::move(rule), std::move(handler));
		else
		{
			auto compiled = compile_pattern(rule, std::move(handler));
			updated->patterns.emplace(std::move(rule), std::move(compiled));
		}
		store_routes(std::move(updated));
		return true;
	}

	void remove_route(const std::string &rule)
	{
		std::lock_guard lock(m_routes_write_mutex);
		auto current = load_routes();

		if( not current->exact.contains(rule) and not current->patterns.contains(rule) )
			return ;

		auto updated = std::make_shared<route_table>(*current);
		updated->exact.erase(rule);
		updated->patterns.erase(rule);
		store_routes(std::move(updated));
	}

public:
	acceptor_wrap_t m_wrap {};
	asio::any_io_executor m_service_exec {};

	server_error_handler_t m_server_error_handler {};
	service_error_handler_t m_service_error_handler {};
	request_handler_t m_default_handler {};

	// A request keeps its immutable snapshot alive while selecting a handler.
	route_table_snapshot m_routes {
		std::make_shared<const route_table>()
	};
	std::mutex m_routes_write_mutex {};
	session_manager m_session_manager {};

	config_t m_config {};
	std::atomic_bool m_is_start {false};
};

template <concepts::any_exec_stream Stream>
basic_server<Stream>::basic_server(acceptor_wrap_t &&wrap, core_concepts::sched auto &&service_exec) :
	m_impl(std::make_shared<impl>(std::move(wrap), asio::any_io_executor (
		get_executor_helper(std::forward<decltype(service_exec)>(service_exec))
	)))
{

}

template <concepts::any_exec_stream Stream>
basic_server<Stream>::basic_server(acceptor_wrap_t &&wrap) :
	m_impl(std::make_shared<impl>(std::move(wrap)))
{

}

template <concepts::any_exec_stream Stream>
basic_server<Stream>::~basic_server() = default;

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::bind(endpoint_wrapper_t ep)
{
	error_code error;
	bind(std::move(ep), error);
	if( error )
	{
		system_error::loc_throw(error,
			"riwo::http::basic_server::bind"
		);
	}
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::bind(endpoint_wrapper_t ep, error_code &error) noexcept
{
	auto &acceptor = m_impl->m_wrap.acceptor();
	if( not acceptor.is_open() )
	{
		if( ep->address().is_v4() )
			acceptor.open(asio::ip::tcp::v4(), error);
		else
			acceptor.open(asio::ip::tcp::v6(), error);
		if( error )
			return *this;
	}
	acceptor.set_option(asio::socket_base::reuse_address(true), error);
	if( error )
		return *this;

	acceptor.bind(std::move(*ep), error);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(size_t max)
{
	error_code error;
	start(max, error);
	if( error )
	{
		system_error::loc_throw(error,
			"riwo::http::basic_server::start"
		);
	}
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(size_t max, error_code &error) noexcept
{
	m_impl->async_start(max, error);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(error_code &error) noexcept
{
	return start(asio::socket_base::max_listen_connections, error);
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto &&service_exec, size_t max)
{
	error_code error;
	start(service_exec, max, error);
	if( error )
	{
		system_error::loc_throw(error,
			"riwo::http::basic_server::start"
		);
	}
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto &&service_exec, size_t max, error_code &error) noexcept
{
	m_impl->async_start (
		get_executor_helper(std::forward<decltype(service_exec)>(service_exec)),
		max, error
	);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto service_exec, error_code &error) noexcept
{
	return start(service_exec, asio::socket_base::max_listen_connections, error);
}

template <concepts::any_exec_stream Stream>
template <method_enum...Method, typename Func, typename...AopPtrs>
basic_server<Stream> &basic_server<Stream>::on_request
(const path_opt_token_t &path_rules, Func &&func, AopPtrs&&...aops) requires
	concepts::request_handler<Func,executor_t> and
	concepts::aop_ptr_list<executor_t,AopPtrs...>
{
	for(auto &path_rule : path_rules.paths)
	{
		if( path_rule.empty() )
		{
			runtime_error::loc_throw (
				"riwo::http::server::on_request: path_rule is empty."
			);
		}
		std::string rule(path_rule.data(), path_rule.size());
		m_impl->rule_path_check(rule);

		auto controller_aop = new impl::multi_ctrlr_aop(func, aops...);
		auto handler = std::make_shared<typename impl::tk_handler>(
			ctrlr_aop_ptr_t(controller_aop)
		);
		handler->template bind_method<Method...>();

		if( not m_impl->add_route(std::move(rule), std::move(handler)) )
		{
			runtime_error::loc_throw (
				"riwo::http::server::on_request: path_rule duplication."
			);
		}
	}
	return *this;
}

template <concepts::any_exec_stream Stream>
template <method_enum...Method>
basic_server<Stream> &basic_server<Stream>::on_request
(const path_opt_token_t &path_rules, ctrlr_aop_ptr_t ctrlr)
{
	for(auto &path_rule : path_rules.paths)
	{
		if( path_rule.empty() )
		{
			runtime_error::loc_throw (
				"riwo::http::server::on_request: path_rule is empty."
			);
		}
		std::string rule(path_rule.data(), path_rule.size());
		m_impl->rule_path_check(rule);

		auto handler = std::make_shared<typename impl::tk_handler>(ctrlr);
		handler->template bind_method<Method...>();

		if( not m_impl->add_route(std::move(rule), std::move(handler)) )
		{
			runtime_error::loc_throw (
				"riwo::http::server::on_request: path_rule duplication."
			);
		}
	}
	return *this;
}

template <concepts::any_exec_stream Stream>
template <method_enum...Method>
basic_server<Stream> &basic_server<Stream>::on_request
(const path_opt_token_t &path_rules, ctrlr_aop_t *ctrlr)
{
	return on_request<Method...>(path_rules, ctrlr_aop_ptr_t(ctrlr));
}

template <concepts::any_exec_stream Stream>
template <typename Func>
basic_server<Stream> &basic_server<Stream>::on_default(Func &&func) requires
	concepts::request_handler<Func,executor_t>
{
	m_impl->m_default_handler = std::forward<Func>(func);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::on_server_error(server_error_handler_t func)
{
	m_impl->m_server_error_handler = std::move(func);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::on_service_error(service_error_handler_t func)
{
	m_impl->m_service_error_handler = std::move(func);
	return *this;
}

template <concepts::any_exec_stream Stream>
template <core_concepts::text_p<char> Text>
basic_server<Stream> &basic_server<Stream>::unbound_request(const Text &path_rule)
{
	auto rule = strtls::to_string(path_rule);
	if( rule.empty() )
	{
		runtime_error::loc_throw (
			"riwo::http::server::unbound_request: path_rule is empty."
		);
	}
	m_impl->rule_path_check(rule);
	m_impl->remove_route(rule);
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::unbound_server_error()
{
	m_impl->m_server_error_handler = {};
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::unbound_service_error()
{
	m_impl->m_service_error_handler = {};
	return *this;
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::set_config(const config_t &config)
{
	m_impl->set_config(config);
	return *this;
}

template <concepts::any_exec_stream Stream>
auto basic_server<Stream>::config() const noexcept -> config_t
{
	return m_impl->config();
}

template <concepts::any_exec_stream Stream>
auto basic_server<Stream>::get_executor() noexcept -> executor_t
{
	return m_impl->m_wrap.acceptor().get_executor();
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::cancel() noexcept
{
	return stop();
}

template <concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::stop() noexcept
{
	m_impl->m_is_start = false;
	error_code ignored {};
	m_impl->m_wrap.acceptor().cancel(ignored);
	return *this;
}

template <concepts::any_exec_stream Stream>
auto basic_server<Stream>::acceptor_wrap() const -> const acceptor_wrap_t &
{
	return m_impl->m_wrap;
}

template <concepts::any_exec_stream Stream>
auto basic_server<Stream>::acceptor_wrap() -> acceptor_wrap_t&
{
	return m_impl->m_wrap;
}

} //namespace riwo::http


#endif //RIWO_HTTP_SERVER_DETAIL_SERVER_H
