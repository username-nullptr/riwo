// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H
#define RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H

#include <riwo/websocket_exp/protocol/handshake.h>
#include <riwo/websocket_exp/detail/handshake_io.h>
#include <riwo/websocket_exp/detail/permessage_deflate.h>
#include <condition_variable>
#include <deque>

namespace riwo::websocket_exp::detail
{

[[nodiscard]] RIWO_WEBSOCKET_EXP_API bool server_equal_ci(
	std::string_view lhs, std::string_view rhs) noexcept;
[[nodiscard]] RIWO_WEBSOCKET_EXP_API bool server_is_h2(
	http_exp::protocol version) noexcept;
[[nodiscard]] RIWO_WEBSOCKET_EXP_API bool server_deadline_expired(
	std::chrono::steady_clock::time_point deadline) noexcept;

inline bool header_has_token(const http_exp::fields &headers,
	std::string_view name, std::string_view wanted) noexcept
{
	for( const auto &field : headers.find_all(name) )
	{
		std::string_view value = field.value.to_string();
		while( not value.empty() )
		{
			auto comma = value.find(',');
			auto token = value.substr(0, comma);
			while( not token.empty() and (token.front() == ' ' or
				token.front() == '\t') ) token.remove_prefix(1);
			while( not token.empty() and (token.back() == ' ' or
				token.back() == '\t') ) token.remove_suffix(1);
			if( server_equal_ci(token, wanted) ) return true;
			if( comma == std::string_view::npos ) break;
			value.remove_prefix(comma + 1);
		}
	}
	return false;
}

struct server_upgrade_plan
{
	request_info request {};
	opening_request opening {};
	opening_response response_message {};
	http_exp::response_head response {};
	std::string body {};
	error_code error {};
	bool accepted = false;
	bool preserve_error_on_write_failure = false;
};

RIWO_WEBSOCKET_EXP_API void reject_upgrade(server_upgrade_plan &plan,
	error_code error,
	http_exp::status status = http_exp::status::bad_request);
RIWO_WEBSOCKET_EXP_API void reject_upgrade(server_upgrade_plan &plan,
	upgrade_rejection rejection, error_code error);
RIWO_WEBSOCKET_EXP_API void reject_selector_exception(
	server_upgrade_plan &plan, const std::exception_ptr &exception) noexcept;
RIWO_WEBSOCKET_EXP_API void select_server_subprotocol(
	server_upgrade_plan &plan, const upgrade_options &options) noexcept;
RIWO_WEBSOCKET_EXP_API void validate_server_subprotocol(
	server_upgrade_plan &plan, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept;
RIWO_WEBSOCKET_EXP_API void select_server_extensions(
	server_upgrade_plan &plan, const upgrade_options &options) noexcept;
RIWO_WEBSOCKET_EXP_API void validate_server_extensions(
	server_upgrade_plan &plan, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept;
RIWO_WEBSOCKET_EXP_API void finalize_server_upgrade_plan(
	server_upgrade_plan &plan, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept;
RIWO_WEBSOCKET_EXP_API void complete_server_upgrade_plan(
	server_upgrade_plan &plan, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline) noexcept;
[[nodiscard]] RIWO_WEBSOCKET_EXP_API server_upgrade_plan
make_server_upgrade_plan(request_info request, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline,
	bool extended_connect, bool permit_async_callbacks = false) noexcept;
[[nodiscard]] RIWO_WEBSOCKET_EXP_API http_exp::server_config
websocket_http_config(http_exp::server_config config);
[[nodiscard]] RIWO_WEBSOCKET_EXP_API server_config
validate_server_config(server_config config);

template <core_concepts::exec Exec>
request_info snapshot_request(
	const http_exp::basic_server_exchange<Exec> &exchange)
{
	request_info result;
	const auto &head = exchange.request();
	result.method = head.method;
	result.version = exchange.selected_protocol();
	result.target = head.target;
	const auto query = result.target.find('?');
	result.path = result.target.substr(0, query);
	if( result.path.empty() ) result.path = "/";
	result.request_headers = head.headers;
	if( result.target.starts_with('/') )
	{
		try {
			result.query_parameters = url(
				std::string("ws://request.invalid") + result.target).parameters();
		}
		catch(...) {}
	}
	else if( head.url.is_valid() )
		result.query_parameters = head.url.parameters();
	result.remote_address = exchange.remote_address();
	result.remote_port = exchange.remote_port();
	error_code endpoint_error;
	result.remote_endpoint = exchange.remote_endpoint(endpoint_error);
	endpoint_error.clear();
	result.local_endpoint = exchange.local_endpoint(endpoint_error);
	return result;
}

template <core_concepts::exec Exec>
server_upgrade_plan prepare_server_upgrade(
	const http_exp::basic_server_exchange<Exec> &exchange,
	const upgrade_options &options, bool permit_async_callbacks) noexcept
{
	if( options.handshake_timeout <= std::chrono::milliseconds::zero() )
	{
		server_upgrade_plan plan;
		reject_upgrade(plan,
			asio::error::make_error_code(asio::error::timed_out));
		return plan;
	}
	const auto deadline = std::chrono::steady_clock::now() +
		options.handshake_timeout;
	const auto &head = exchange.request();
	const bool extended = server_is_h2(exchange.selected_protocol()) and
		head.connect_protocol and
		server_equal_ci(*head.connect_protocol, "websocket");
	return make_server_upgrade_plan(snapshot_request(exchange), options,
		deadline, extended, permit_async_callbacks);
}

template <core_concepts::exec Exec>
void write_rejection(http_exp::basic_server_exchange<Exec> &exchange,
	server_upgrade_plan &plan, error_code &error) noexcept
{
	exchange.respond(std::move(plan.response), asio::buffer(plan.body), error);
	if( not error or plan.preserve_error_on_write_failure )
		error = plan.error;
}

template <core_concepts::exec Exec>
void upgrade_sync(http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, basic_accept_result<Exec> &result,
	error_code &error) noexcept
{
	try {
		result.stream = basic_stream<Exec>(exchange.get_executor(), options.stream);
		auto plan = prepare_server_upgrade(exchange, options, false);
		result.request = plan.request;
		if( not plan.accepted )
		{
			if( plan.error == asio::error::make_error_code(
				asio::error::timed_out) )
			{
				exchange.cancel();
				error = plan.error;
				return;
			}
			write_rejection(exchange, plan, error);
			return;
		}
		auto channel = exchange.accept_duplex(
			std::move(plan.response), error);
		if( error ) return;
		result.handshake.subprotocol =
			plan.response_message.subprotocol.value_or("");
		result.handshake.extensions = plan.response_message.extensions;
		result.stream.adopt(
			std::make_shared<http_exp::basic_duplex_channel<Exec>>(
				std::move(channel)), adopt_options {
					.stream_role = role::server,
					.negotiated_subprotocol = result.handshake.subprotocol,
					.negotiated_extensions = result.handshake.extensions,
				}, error);
	}
	catch(...) { error = exception_error(std::current_exception()); }
}

template <core_concepts::exec Exec>
asio::awaitable<optional<std::tuple<error_code,basic_accept_result<Exec>>>,Exec>
co_upgrade(http_exp::basic_server_exchange<Exec> *exchange,
	upgrade_options options)
{
	using result_t = basic_accept_result<Exec>;
	result_t result(exchange->get_executor());
	try {
		result.stream = basic_stream<Exec>(exchange->get_executor(), options.stream);
		const auto deadline = std::chrono::steady_clock::now() +
			options.handshake_timeout;
		auto plan = prepare_server_upgrade(*exchange, options, true);

		if( plan.accepted and options.async_request_validator )
		{
			try {
				if( auto rejection = co_await asio::co_spawn(
					exchange->get_executor(),
					options.async_request_validator(plan.request),
					asio::use_awaitable_t<Exec>{}) )
					reject_upgrade(plan, std::move(*rejection),
						make_error_code(errc::handshake_rejected));
			}
			catch(...) {
				reject_upgrade(plan, exception_error(std::current_exception()),
					http_exp::status::internal_server_error);
				plan.preserve_error_on_write_failure = true;
			}
		}
		if( plan.accepted and server_deadline_expired(deadline) )
			reject_upgrade(plan,
				asio::error::make_error_code(asio::error::timed_out));

		if( plan.accepted and options.async_origin_validator )
		{
			optional<std::string> origin;
			if( auto value = plan.request.request_headers.find_first("Origin") )
				origin = std::string(*value);
			try {
				if( auto rejection = co_await asio::co_spawn(
					exchange->get_executor(),
					options.async_origin_validator(std::move(origin)),
					asio::use_awaitable_t<Exec>{}) )
					reject_upgrade(plan, std::move(*rejection),
						make_error_code(errc::handshake_rejected));
			}
			catch(...) {
				reject_upgrade(plan, exception_error(std::current_exception()),
					http_exp::status::internal_server_error);
				plan.preserve_error_on_write_failure = true;
			}
		}
		if( plan.accepted and server_deadline_expired(deadline) )
			reject_upgrade(plan,
				asio::error::make_error_code(asio::error::timed_out));

		if( plan.accepted and options.async_subprotocol_selector )
		{
			try {
				plan.response_message.subprotocol = co_await asio::co_spawn(
					exchange->get_executor(),
					options.async_subprotocol_selector(plan.request,
						plan.opening.subprotocols),
					asio::use_awaitable_t<Exec>{});
			}
			catch(...) {
				reject_selector_exception(plan, std::current_exception());
			}
		}
		else if( plan.accepted ) select_server_subprotocol(plan, options);
		validate_server_subprotocol(plan, options, deadline);

		if( plan.accepted and options.async_extension_selector )
		{
			try {
				plan.response_message.extensions = co_await asio::co_spawn(
					exchange->get_executor(),
					options.async_extension_selector(plan.request,
						plan.opening.extensions),
					asio::use_awaitable_t<Exec>{});
			}
			catch(...) {
				reject_selector_exception(plan, std::current_exception());
			}
		}
		else if( plan.accepted ) select_server_extensions(plan, options);
		validate_server_extensions(plan, options, deadline);
		finalize_server_upgrade_plan(plan, options, deadline);
		result.request = plan.request;

		if( not plan.accepted )
		{
			if( plan.error == asio::error::make_error_code(
				asio::error::timed_out) )
			{
				exchange->cancel();
				co_return std::tuple {plan.error, std::move(result)};
			}
			auto original_error = plan.error;
			const bool preserve = plan.preserve_error_on_write_failure;
			auto [write_error] = co_await exchange->respond(
				std::move(plan.response), asio::buffer(plan.body),
				asio::as_tuple(asio::use_awaitable_t<Exec>{}));
			co_return std::tuple {
				write_error and not preserve ? write_error : original_error,
				std::move(result)};
		}

		auto [accept_error, channel] = co_await exchange->accept_duplex(
			std::move(plan.response),
			asio::as_tuple(asio::use_awaitable_t<Exec>{}));
		if( accept_error ) co_return std::tuple {
			accept_error, std::move(result)};
		result.handshake.subprotocol =
			plan.response_message.subprotocol.value_or("");
		result.handshake.extensions = plan.response_message.extensions;
		error_code adopt_error;
		result.stream.adopt(
			std::make_shared<http_exp::basic_duplex_channel<Exec>>(
				std::move(channel)), adopt_options {
					.stream_role = role::server,
					.negotiated_subprotocol = result.handshake.subprotocol,
					.negotiated_extensions = result.handshake.extensions,
				}, adopt_error);
		co_return std::tuple {adopt_error, std::move(result)};
	}
	catch(...) {
		co_return std::tuple {exception_error(std::current_exception()),
			std::move(result)};
	}
}

template <core_concepts::exec Exec, typename Handler>
void start_upgrade(http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, Handler &&handler)
{
	auto executor = exchange.get_executor();
	auto coroutine_cancellation =
		std::make_shared<asio::cancellation_signal>();
	auto slot = asio::get_associated_cancellation_slot(handler);
	if( slot.is_connected() )
	{
		std::weak_ptr<asio::cancellation_signal> weak =
			coroutine_cancellation;
		slot.assign([exchange = std::addressof(exchange), weak](
			asio::cancellation_type type) noexcept
		{
			if( type == asio::cancellation_type::none ) return;
			exchange->cancel();
			if( auto signal = weak.lock() ) signal->emit(type);
		});
	}
	auto completion =
	[handler = std::forward<Handler>(handler), executor, slot,
	 coroutine_cancellation]
	(std::exception_ptr exception,
	 optional<std::tuple<error_code,basic_accept_result<Exec>>> value) mutable
	{
		ignore_unused(coroutine_cancellation);
		if( slot.is_connected() ) slot.clear();
		if( exception ) std::move(handler)(exception_error(exception),
			basic_accept_result<Exec>(executor));
		else if( value ) std::move(handler)(std::get<0>(*value),
			std::move(std::get<1>(*value)));
		else std::move(handler)(asio::error::operation_aborted,
			basic_accept_result<Exec>(executor));
	};
	asio::co_spawn(executor, co_upgrade(&exchange, std::move(options)),
		asio::bind_cancellation_slot(coroutine_cancellation->slot(),
			std::move(completion)));
}

} //namespace riwo::websocket_exp::detail

namespace riwo::websocket_exp
{

template <core_concepts::exec Exec>
bool is_upgrade_request(
	const http_exp::basic_server_exchange<Exec> &exchange) noexcept
{
	const auto &request = exchange.request();
	if( detail::server_is_h2(exchange.selected_protocol()) )
		return request.method.known() == http_exp::method::connect and
			request.connect_protocol and
			detail::server_equal_ci(*request.connect_protocol, "websocket");
	return request.method.known() == http_exp::method::get and
		detail::header_has_token(request.headers, "Connection", "Upgrade") and
		detail::header_has_token(request.headers, "Upgrade", "websocket");
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http_exp::basic_server_exchange<Exec> &exchange, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>> {
	return upgrade(exchange, upgrade_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http_exp::basic_server_exchange<Exec> &exchange,
	upgrade_options options, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_accept_result<Exec>>
{
	using result_t = basic_accept_result<Exec>;
	if constexpr( is_error_code_token_v<Token> )
	{
		result_t result(exchange.get_executor());
		auto adapted_error = adapt_error_code(token);
		detail::upgrade_sync(exchange, std::move(options), result,
			adapted_error.get());
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		result_t result(exchange.get_executor());
		detail::upgrade_sync(exchange, std::move(options), result, error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::upgrade");
		return result;
	}
	else
	{
		const auto timeout = options.handshake_timeout;
		return detail::initiate_handshake_io<result_t>(exchange.get_executor(),
		[&exchange, options = std::move(options)](auto handler) mutable {
			detail::start_upgrade(exchange, std::move(options),
				std::move(handler));
		}, timeout, nullopt,
		unbound_redirect_time(std::forward<Token>(token)));
	}
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
class basic_server<Exec,ServiceExec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	enum class delivery_mode : uint8_t { unset, accept, handler };

	struct accept_waiter
	{
		upgrade_options_t options {};
		asio::cancellation_signal cancellation {};
		asio::cancellation_slot caller_slot {};
		std::function<void(error_code,accept_result_t)> completion {};
		std::atomic_bool cancelled {false};
		std::atomic_bool completed {false};

		void complete(error_code error, accept_result_t result) noexcept
		{
			if( completed.exchange(true, std::memory_order_acq_rel) ) return;
			if( caller_slot.is_connected() ) caller_slot.clear();
			try { completion(error, std::move(result)); }
			catch(...) { forced_termination(); }
		}
	};

	struct pending_handshake
	{
		explicit pending_handshake(const service_executor_t &executor) :
			timer(executor) {}
		asio::steady_timer timer;
		std::shared_ptr<accept_waiter> waiter {};
		std::chrono::milliseconds rejection_timeout {};
		bool queued = false;
		bool stopped = false;
	};

	struct acquisition
	{
		std::shared_ptr<accept_waiter> waiter {};
		std::chrono::milliseconds rejection_timeout {};
		bool stopped = false;
	};

public:
	impl(acceptor_wrap_t &&acceptor, service_executor_t service_executor,
		config_t config) :
		m_config(detail::validate_server_config(std::move(config))),
		m_http_server(std::move(acceptor), std::move(service_executor),
			m_config.http) {}

	impl(acceptor_wrap_t &&acceptor, config_t config) :
		m_config(detail::validate_server_config(std::move(config))),
		m_http_server(std::move(acceptor), m_config.http) {}

	impl(std::shared_ptr<transport_listener_t> listener,
		service_executor_t service_executor, config_t config) :
		m_config(detail::validate_server_config(std::move(config))),
		m_http_server(std::move(listener), std::move(service_executor),
			m_config.http) {}

	impl(std::shared_ptr<transport_listener_t> listener, config_t config) :
		m_config(detail::validate_server_config(std::move(config))),
		m_http_server(std::move(listener), m_config.http) {}

	[[nodiscard]] accept_result_t idle_result() const noexcept {
		return accept_result_t(m_http_server.get_service_executor());
	}

	[[nodiscard]] bool enter_accept_mode()
	{
		bool install_handler = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_mode == delivery_mode::handler ) return false;
			if( m_mode == delivery_mode::unset )
			{
				m_mode = delivery_mode::accept;
				install_handler = true;
			}
		}
		if( install_handler )
		{
			std::weak_ptr<impl> weak = this->shared_from_this();
			m_http_server.on_default(
			[weak](context_t &context) -> awaitable<void>
			{
				if( auto self = weak.lock() )
					co_await self->serve_accept(context);
				else context.cancel();
			});
		}
		return true;
	}

	void enter_handler_mode()
	{
		std::lock_guard lock(m_mutex);
		if( m_mode == delivery_mode::accept )
			throw std::logic_error(
				"riwo::websocket_exp::basic_server: accept mode is already active");
		m_mode = delivery_mode::handler;
	}

	void post_timer_cancel(const std::shared_ptr<pending_handshake> &pending)
	{
		asio::post(m_http_server.get_service_executor(), [pending]
		{
			try { ignore_unused(pending->timer.cancel()); }
			catch(...) {}
		});
	}

	[[nodiscard]] std::shared_ptr<pending_handshake>
	pop_pending_handshake_locked()
	{
		while( not m_pending_handshakes.empty() )
		{
			auto pending = m_pending_handshakes.front();
			m_pending_handshakes.pop_front();
			if( not pending->queued ) continue;
			pending->queued = false;
			return pending;
		}
		return {};
	}

	[[nodiscard]] std::shared_ptr<accept_waiter> pop_accept_locked()
	{
		while( not m_accepts.empty() )
		{
			auto waiter = m_accepts.front();
			m_accepts.pop_front();
			if( waiter->cancelled.load(std::memory_order_acquire) or
				waiter->completed.load(std::memory_order_acquire) ) continue;
			return waiter;
		}
		return {};
	}

	void enqueue_accept(const std::shared_ptr<accept_waiter> &waiter,
		bool front = false)
	{
		std::shared_ptr<pending_handshake> pending;
		bool abort = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_stopped or waiter->cancelled.load(std::memory_order_acquire) )
				abort = true;
			else if( (pending = pop_pending_handshake_locked()) )
				pending->waiter = waiter;
			else if( front ) m_accepts.emplace_front(waiter);
			else m_accepts.emplace_back(waiter);
		}
		if( pending ) post_timer_cancel(pending);
		else if( abort ) waiter->complete(
			asio::error::operation_aborted, idle_result());
	}

	void cancel_waiter(const std::shared_ptr<accept_waiter> &waiter) noexcept
	{
		if( waiter->cancelled.exchange(true, std::memory_order_acq_rel) ) return;
		{
			std::lock_guard lock(m_mutex);
			auto iterator = std::find(m_accepts.begin(), m_accepts.end(), waiter);
			if( iterator != m_accepts.end() ) m_accepts.erase(iterator);
		}
		waiter->cancellation.emit(asio::cancellation_type::all);
		waiter->complete(asio::error::operation_aborted, idle_result());
	}

	template <typename Handler>
	[[nodiscard]] std::shared_ptr<accept_waiter> make_waiter(
		upgrade_options_t options, Handler &&handler)
	{
		using handler_t = std::remove_cvref_t<Handler>;
		auto owned_handler = std::make_shared<handler_t>(
			std::forward<Handler>(handler));
		auto waiter = std::make_shared<accept_waiter>();
		waiter->options = std::move(options);
		waiter->caller_slot = asio::get_associated_cancellation_slot(
			*owned_handler);
		auto completion_executor = asio::get_associated_executor(
			*owned_handler, m_http_server.get_service_executor());
		auto allocator = asio::get_associated_allocator(*owned_handler);
		waiter->completion = [owned_handler, completion_executor, allocator]
		(error_code error, accept_result_t result) mutable
		{
			asio::post(completion_executor, asio::bind_allocator(allocator,
			[owned_handler, error, result = std::move(result)]() mutable {
				std::move(*owned_handler)(error, std::move(result));
			}));
		};
		if( waiter->caller_slot.is_connected() )
		{
			std::weak_ptr<impl> weak_self = this->shared_from_this();
			std::weak_ptr<accept_waiter> weak_waiter = waiter;
			waiter->caller_slot.assign(
			[weak_self, weak_waiter](asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none ) return;
				if( auto self = weak_self.lock() )
					if( auto active = weak_waiter.lock() )
						self->cancel_waiter(active);
			});
		}
		std::lock_guard lock(m_mutex);
		std::erase_if(m_all_accepts,
			[](const auto &item) { return item.expired(); });
		m_all_accepts.emplace_back(waiter);
		return waiter;
	}

	[[nodiscard]] awaitable<acquisition> acquire_accept()
	{
		std::shared_ptr<pending_handshake> pending;
		{
			std::lock_guard lock(m_mutex);
			if( m_stopped ) co_return acquisition {.stopped = true};
			if( auto waiter = pop_accept_locked() )
				co_return acquisition {.waiter = std::move(waiter)};
			const auto config = m_config;
			if( config.max_pending_handshakes == 0 or
				config.pending_handshake_timeout <=
					std::chrono::milliseconds::zero() or
				m_pending_handshakes.size() >= config.max_pending_handshakes )
				co_return acquisition {
					.rejection_timeout =
						config.default_upgrade.handshake_timeout};
			pending = std::make_shared<pending_handshake>(
				m_http_server.get_service_executor());
			pending->timer.expires_after(config.pending_handshake_timeout);
			pending->rejection_timeout =
				config.default_upgrade.handshake_timeout;
			pending->queued = true;
			m_pending_handshakes.emplace_back(pending);
		}
		auto [timer_error] = co_await pending->timer.async_wait(
			asio::as_tuple(use_awaitable));
		ignore_unused(timer_error);
		std::lock_guard lock(m_mutex);
		if( pending->waiter )
			co_return acquisition {.waiter = std::move(pending->waiter)};
		if( pending->queued )
		{
			auto iterator = std::find(m_pending_handshakes.begin(),
				m_pending_handshakes.end(), pending);
			if( iterator != m_pending_handshakes.end() )
				m_pending_handshakes.erase(iterator);
			pending->queued = false;
		}
		co_return acquisition {
			.rejection_timeout = pending->rejection_timeout,
			.stopped = pending->stopped or m_stopped};
	}

	awaitable<void> reject_unavailable(context_t &context,
		std::chrono::milliseconds timeout)
	{
		if( timeout <= std::chrono::milliseconds::zero() )
		{
			context.cancel();
			co_return;
		}
		static constexpr std::string_view body =
			"WebSocket accept queue unavailable\n";
		http_exp::response_head response;
		response.status = http_exp::status::service_unavailable;
		if( not detail::server_is_h2(context.selected_protocol()) )
			response.headers.set("Connection", "close");
		auto [error] = co_await context.respond(std::move(response),
			asio::buffer(body), asio::as_tuple(use_awaitable));
		ignore_unused(error);
		context.cancel();
	}

	awaitable<void> serve_accept(context_t &context)
	{
		if( not is_upgrade_request(context) )
		{
			upgrade_options_t options;
			{
				std::lock_guard lock(m_mutex);
				options = m_config.default_upgrade;
			}
			auto result = co_await websocket_exp::upgrade(context,
				std::move(options), asio::as_tuple(use_awaitable));
			ignore_unused(result);
			co_return;
		}
		auto acquired = co_await acquire_accept();
		if( acquired.stopped )
		{
			context.cancel();
			co_return;
		}
		if( not acquired.waiter )
		{
			co_await reject_unavailable(context, acquired.rejection_timeout);
			co_return;
		}
		auto waiter = std::move(acquired.waiter);
		if( waiter->cancelled.load(std::memory_order_acquire) )
		{
			waiter->complete(asio::error::operation_aborted, idle_result());
			co_await reject_unavailable(context,
				config_snapshot().default_upgrade.handshake_timeout);
			co_return;
		}
		auto upgraded = co_await websocket_exp::upgrade(context,
			waiter->options, asio::bind_cancellation_slot(
				waiter->cancellation.slot(), asio::as_tuple(use_awaitable)));
		auto &[error, result] = upgraded;
		if( not error )
		{
			waiter->complete({}, std::move(result));
			co_return;
		}
		if( waiter->cancelled.load(std::memory_order_acquire) or stopped() )
		{
			waiter->complete(asio::error::operation_aborted,
				std::move(result));
			co_return;
		}
		// Rejected or malformed handshakes do not consume a queued accept.
		enqueue_accept(waiter, true);
	}

	[[nodiscard]] bool stopped() const noexcept
	{
		std::lock_guard lock(m_mutex);
		return m_stopped;
	}

	[[nodiscard]] config_t config_snapshot() const
	{
		std::lock_guard lock(m_mutex);
		return m_config;
	}

	void set_config(config_t config)
	{
		config = detail::validate_server_config(std::move(config));
		m_http_server.set_config(config.http);
		std::lock_guard lock(m_mutex);
		m_config = std::move(config);
	}

	template <typename Token>
	[[nodiscard]] auto async_accept(upgrade_options_t options, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto self = this->shared_from_this();
		return asio::async_initiate<token_t,
			void(error_code,accept_result_t)>(
		[self = std::move(self), options = std::move(options)]
		(auto completion_handler) mutable
		{
			auto waiter = self->make_waiter(std::move(options),
				std::move(completion_handler));
			if( not self->enter_accept_mode() )
			{
				waiter->complete(make_system_error_code(
					std::errc::operation_not_supported),
					self->idle_result());
				return;
			}
			self->enqueue_accept(waiter);
		}, completion_token);
	}

	[[nodiscard]] accept_result_t accept_sync(upgrade_options_t options,
		error_code &error) noexcept
	{
		struct sync_state
		{
			std::mutex mutex;
			std::condition_variable changed;
			std::unique_ptr<accept_result_t> result;
			error_code error;
		};
		auto state = std::make_shared<sync_state>();
		try {
			auto waiter = make_waiter(std::move(options),
			[state](error_code accept_error, accept_result_t result) mutable
			{
				{
					std::lock_guard lock(state->mutex);
					state->error = accept_error;
					state->result = std::make_unique<accept_result_t>(
						std::move(result));
				}
				state->changed.notify_one();
			});
			// Synchronous accept waits on the caller and therefore completes
			// directly instead of posting back to the same executor.
			waiter->completion =
			[state](error_code accept_error, accept_result_t result) mutable
			{
				{
					std::lock_guard lock(state->mutex);
					state->error = accept_error;
					state->result = std::make_unique<accept_result_t>(
						std::move(result));
				}
				state->changed.notify_one();
			};
			if( not enter_accept_mode() )
				waiter->complete(make_system_error_code(
					std::errc::operation_not_supported), idle_result());
			else enqueue_accept(waiter);
			std::unique_lock lock(state->mutex);
			state->changed.wait(lock,
				[&] { return static_cast<bool>(state->result); });
			error = state->error;
			return std::move(*state->result);
		}
		catch(...) { error = exception_error(std::current_exception()); }
		return idle_result();
	}

	template <typename Func>
	void bind_connection(std::string path, Func &&func,
		optional<upgrade_options_t> options)
	{
		enter_handler_mode();
		auto selected = options.value_or(config_snapshot().default_upgrade);
		auto handler = std::make_shared<std::remove_cvref_t<Func>>(
			std::forward<Func>(func));
		m_http_server.template on_request<http_exp::method::get,
			http_exp::method::connect>(std::move(path),
		[handler, selected = std::move(selected)]
		(context_t &context) mutable -> awaitable<void>
		{
			auto upgraded = co_await websocket_exp::upgrade(context,
				selected, asio::as_tuple(use_awaitable));
			auto &[error, result] = upgraded;
			if( not error ) co_await (*handler)(std::move(result));
		});
	}

	template <typename Func>
	void bind_default(Func &&func, optional<upgrade_options_t> options)
	{
		enter_handler_mode();
		auto selected = options.value_or(config_snapshot().default_upgrade);
		auto handler = std::make_shared<std::remove_cvref_t<Func>>(
			std::forward<Func>(func));
		m_http_server.on_default(
		[handler, selected = std::move(selected)]
		(context_t &context) mutable -> awaitable<void>
		{
			auto upgraded = co_await websocket_exp::upgrade(context,
				selected, asio::as_tuple(use_awaitable));
			auto &[error, result] = upgraded;
			if( not error ) co_await (*handler)(std::move(result));
		});
	}

	void cancel_all() noexcept
	{
		std::vector<std::shared_ptr<accept_waiter>> accepts;
		std::deque<std::shared_ptr<pending_handshake>> handshakes;
		{
			std::lock_guard lock(m_mutex);
			m_stopped = true;
			for( auto &item : m_all_accepts )
				if( auto waiter = item.lock(); waiter and
					not waiter->completed.load(std::memory_order_acquire) )
					accepts.emplace_back(std::move(waiter));
			m_accepts.clear();
			m_all_accepts.clear();
			handshakes.swap(m_pending_handshakes);
			for( auto &pending : handshakes )
			{
				pending->queued = false;
				pending->stopped = true;
			}
		}
		for( auto &waiter : accepts )
		{
			waiter->cancelled.store(true, std::memory_order_release);
			waiter->cancellation.emit(asio::cancellation_type::all);
			waiter->complete(asio::error::operation_aborted, idle_result());
		}
		for( auto &pending : handshakes ) post_timer_cancel(pending);
	}

	void stop() noexcept
	{
		cancel_all();
		m_http_server.stop();
	}

	config_t m_config {};
	http_server_t m_http_server;
	mutable std::mutex m_mutex;
	delivery_mode m_mode = delivery_mode::unset;
	bool m_stopped = false;
	std::deque<std::shared_ptr<accept_waiter>> m_accepts {};
	std::deque<std::shared_ptr<pending_handshake>> m_pending_handshakes {};
	std::vector<std::weak_ptr<accept_waiter>> m_all_accepts {};
};

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::basic_server()
	requires (
		core_concepts::match_sched<io_executor_t,executor_t> and
		core_concepts::match_sched<executor_t,service_executor_t>) :
	basic_server(acceptor_wrap_t{})
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
basic_server<Exec,ServiceExec>::basic_server(
	std::shared_ptr<transport_listener_t> listener, config_t config)
	requires core_concepts::match_sched<executor_t,service_executor_t> :
	m_impl(std::make_shared<impl>(std::move(listener), std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
basic_server<Exec,ServiceExec>::basic_server(
	std::shared_ptr<transport_listener_t> listener,
	ServiceExec0 &&service_executor, config_t config)
	requires core_concepts::match_sched<ServiceExec0,service_executor_t> :
	m_impl(std::make_shared<impl>(std::move(listener),
		get_executor_helper(std::forward<ServiceExec0>(service_executor)),
		std::move(config)))
{
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
basic_server<Exec,ServiceExec>::~basic_server()
{
	if( m_impl ) m_impl->stop();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_t endpoint)
	-> basic_server&
{
	m_impl->m_http_server.bind(std::move(endpoint));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::bind(endpoint_wrapper_t endpoint,
	error_code &error) noexcept -> basic_server&
{
	m_impl->m_http_server.bind(std::move(endpoint), error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t maximum) -> basic_server&
{
	m_impl->m_http_server.start(maximum);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(size_t maximum,
	error_code &error) noexcept -> basic_server&
{
	m_impl->m_http_server.start(maximum, error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::start(error_code &error) noexcept
	-> basic_server&
{
	m_impl->m_http_server.start(error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::serve(
	http_exp::basic_negotiated_transport<executor_t> transport)
	-> basic_server&
{
	m_impl->m_http_server.serve(std::move(transport));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::serve(
	http_exp::basic_negotiated_transport<executor_t> transport,
	error_code &error) noexcept -> basic_server&
{
	m_impl->m_http_server.serve(std::move(transport), error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t maximum) -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
{
	m_impl->m_http_server.start(
		std::forward<ServiceExec0>(service_executor), maximum);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	size_t maximum, error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
{
	m_impl->m_http_server.start(
		std::forward<ServiceExec0>(service_executor), maximum, error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename ServiceExec0>
auto basic_server<Exec,ServiceExec>::start(ServiceExec0 &&service_executor,
	error_code &error) noexcept -> basic_server&
	requires core_concepts::match_sched<ServiceExec0,service_executor_t>
{
	m_impl->m_http_server.start(
		std::forward<ServiceExec0>(service_executor), error);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Token>
auto basic_server<Exec,ServiceExec>::accept(Token &&token)
	requires accept_token_v<Token>
{
	return accept(config().default_upgrade, std::forward<Token>(token));
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Token>
auto basic_server<Exec,ServiceExec>::accept(upgrade_options_t options,
	Token &&token) requires accept_token_v<Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->accept_sync(std::move(options), adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->accept_sync(std::move(options), error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::basic_server::accept");
		return result;
	}
	else return m_impl->async_accept(std::move(options),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
size_t basic_server<Exec,ServiceExec>::pending_accept_count() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_accepts.size();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
size_t basic_server<Exec,ServiceExec>::pending_handshake_count() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_pending_handshakes.size();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_connection(std::string path,
	Func &&func, optional<upgrade_options_t> options) -> basic_server&
	requires connection_handler_v<Func>
{
	m_impl->bind_connection(std::move(path), std::forward<Func>(func),
		std::move(options));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
template <typename Func>
auto basic_server<Exec,ServiceExec>::on_default(Func &&func,
	optional<upgrade_options_t> options) -> basic_server&
	requires connection_handler_v<Func>
{
	m_impl->bind_default(std::forward<Func>(func), std::move(options));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_connection(std::string_view path)
	-> basic_server&
{
	if( path.empty() ) m_impl->m_http_server.on_default(
		[](context_t&) -> awaitable<void> { co_return; });
	else m_impl->m_http_server.unbound_request(path);
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_server_error(
	server_error_handler_t handler) -> basic_server&
{
	m_impl->m_http_server.on_server_error(std::move(handler));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::on_service_error(
	service_error_handler_t handler) -> basic_server&
{
	m_impl->m_http_server.on_service_error(std::move(handler));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_server_error() -> basic_server&
{
	m_impl->m_http_server.unbound_server_error();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::unbound_service_error() -> basic_server&
{
	m_impl->m_http_server.unbound_service_error();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::set_config(config_t config)
	-> basic_server&
{
	m_impl->set_config(std::move(config));
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::config() const -> config_t {
	return m_impl->config_snapshot();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::cancel() noexcept -> basic_server&
{
	m_impl->stop();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::stop() noexcept -> basic_server&
{
	m_impl->stop();
	return *this;
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::http_server() const noexcept
	-> const http_server_t& { return m_impl->m_http_server; }

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::http_server() noexcept
	-> http_server_t& { return m_impl->m_http_server; }

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_executor() noexcept -> executor_t {
	return m_impl->m_http_server.get_executor();
}

template <core_concepts::exec Exec, core_concepts::exec ServiceExec>
auto basic_server<Exec,ServiceExec>::get_service_executor() const noexcept
	-> service_executor_t {
	return m_impl->m_http_server.get_service_executor();
}

} //namespace riwo::websocket_exp

#endif //RIWO_WEBSOCKET_EXP_DETAIL_SERVER_H
