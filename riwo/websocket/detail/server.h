// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_SERVER_H
#define RIWO_WEBSOCKET_DETAIL_SERVER_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <riwo/websocket/protocol/handshake.h>
#include <riwo/websocket/detail/permessage_deflate.h>
#include <riwo/websocket/detail/handshake_io.h>

namespace riwo::websocket { namespace detail
{

constexpr const char
	*sec_websocket_accept = "Sec-WebSocket-Accept",
	*sec_websocket_version = "Sec-WebSocket-Version",
	*sec_websocket_protocol = "Sec-WebSocket-Protocol",
	*sec_websocket_extensions = "Sec-WebSocket-Extensions";

[[nodiscard]] RIWO_WEBSOCKET_API bool server_ascii_equal_case_insensitive (
	std::string_view lhs, std::string_view rhs
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API bool server_deadline_expired (
	std::chrono::steady_clock::time_point deadline
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API std::chrono::milliseconds server_remaining_timeout (
	std::chrono::steady_clock::time_point deadline
) noexcept;

template <typename Exec>
RIWO_WEBSOCKET_TAPI void close_upgrade_connection(http::basic_service_context<Exec> &context) noexcept
{
	auto connection = context.hand_over_connection();
	context.request().cancel();
	context.response().cancel();

	if( connection )
	{
		ignore_unused(connection->cancel());
		ignore_unused(connection->close());
	}
}

template <typename Exec>
[[nodiscard]] RIWO_WEBSOCKET_TAPI
request_info snapshot_request(const http::basic_request<Exec> &request)
{
	return request_info {
		.method = request.method(),
		.version = request.version(),
		.target = std::string(request.target()),
		.path = std::string(request.path()),
		.request_headers = request.headers(),
		.query_parameters = request.parameters(),
		.path_arguments = request.path_args(),
		.remote_endpoint = request.remote_endpoint(),
		.local_endpoint = request.local_endpoint()
	};
}

[[nodiscard]] RIWO_WEBSOCKET_API
bool valid_rejection_status(http::status_enum status) noexcept;

RIWO_WEBSOCKET_API
void erase_protocol_response_headers(http::headers &headers) noexcept;

struct server_upgrade_plan
{
	request_info request {};
	opening_request opening {};
	opening_response response {};

	http::status_enum status = http::status::bad_request;
	http::headers headers {};
	std::string body {};

	error_code error {};
	bool accepted = false;
	bool preserve_error_on_write_failure = false;
};

RIWO_WEBSOCKET_API void reject_upgrade(server_upgrade_plan &plan,
	error_code error, http::status_enum status = http::status::bad_request
);
RIWO_WEBSOCKET_API void reject_upgrade(server_upgrade_plan &plan,
	upgrade_rejection rejection, error_code error
);

RIWO_WEBSOCKET_API void reject_selector_exception (
	server_upgrade_plan &plan, const std::exception_ptr &exception
) noexcept;

RIWO_WEBSOCKET_API void select_server_subprotocol (
	server_upgrade_plan &plan, const upgrade_options &options
) noexcept;

RIWO_WEBSOCKET_API void validate_server_subprotocol(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline
) noexcept;

RIWO_WEBSOCKET_API void select_server_extensions (
	server_upgrade_plan &plan, const upgrade_options &options
) noexcept;

RIWO_WEBSOCKET_API void validate_server_extensions(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline
) noexcept;

RIWO_WEBSOCKET_API void finalize_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline
) noexcept;

RIWO_WEBSOCKET_API void complete_server_upgrade_plan(server_upgrade_plan &plan,
	const upgrade_options &options, std::chrono::steady_clock::time_point deadline
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API server_upgrade_plan make_server_upgrade_plan (
	request_info request, const upgrade_options &options, std::chrono::steady_clock::time_point deadline,
	bool permit_async_callbacks = false
) noexcept;

template <typename Exec>
[[nodiscard]] RIWO_WEBSOCKET_TAPI server_upgrade_plan make_server_upgrade_plan(
	http::basic_service_context<Exec> &context, const upgrade_options &options,
	std::chrono::steady_clock::time_point deadline, bool permit_async_callbacks = false) noexcept
{
	return make_server_upgrade_plan(snapshot_request(context.request()),
		options, deadline, permit_async_callbacks
	);
}

template <typename Exec>
RIWO_WEBSOCKET_TAPI void
prepare_response(http::basic_response<Exec> &response, const server_upgrade_plan &plan)
{
	response.unset_header(http::header::content_length);
	response.unset_header(http::header::transfer_encoding);

	response.unset_header(http::header::connection);
	response.unset_header(http::header::upgrade);

	response.unset_header(sec_websocket_accept);
	response.unset_header(sec_websocket_version);

	response.unset_header(sec_websocket_protocol);
	response.unset_header(sec_websocket_extensions);

	response.set_status(plan.status);
	for(const auto &[name, value] : plan.headers)
		response.set_header(name, value);
}

template <typename Exec>
RIWO_WEBSOCKET_TAPI void upgrade_sync(http::basic_service_context<Exec> &context,
	upgrade_options options, basic_accept_result<Exec> &result, error_code &error) noexcept
{
	try {
		result.stream = basic_stream<Exec>(context.get_executor(), options.stream);
		if( options.handshake_timeout <= std::chrono::milliseconds::zero() )
		{
			close_upgrade_connection(context);
			error = asio::error::timed_out;
			return ;
		}
		const auto deadline = std::chrono::steady_clock::now() + options.handshake_timeout;
		auto plan = make_server_upgrade_plan(context, options, deadline);

		result.request = plan.request;
		if( plan.error == asio::error::timed_out or server_deadline_expired(deadline) )
		{
			close_upgrade_connection(context);
			error = asio::error::timed_out;
			return ;
		}
		prepare_response(context.response(), plan);
		ignore_unused(context.response().write(asio::buffer(plan.body), error));
		if( error )
		{
			close_upgrade_connection(context);
			if( plan.preserve_error_on_write_failure )
				error = plan.error;
			return ;
		}
		if( server_deadline_expired(deadline) )
		{
			close_upgrade_connection(context);
			error = asio::error::timed_out;
			return ;
		}
		if( not plan.accepted )
		{
			error = plan.error;
			return ;
		}
		auto pending = context.request().take_pending_data();
		auto connection = context.hand_over_connection();

		adopt_options adopt {
			.stream_role = role::server,
			.pending_data = std::vector<std::byte>(pending.size()),
			.negotiated_subprotocol = plan.response.subprotocol.value_or(""),
			.negotiated_extensions = plan.response.extensions,
		};
		if( not pending.empty() )
			std::memcpy(adopt.pending_data.data(), pending.data(), pending.size());

		result.handshake.subprotocol = adopt.negotiated_subprotocol;
		result.handshake.extensions = adopt.negotiated_extensions;
		result.stream.adopt(std::move(connection), std::move(adopt), error);
	}
	catch(...)
	{
		error = exception_error(std::current_exception());
		close_upgrade_connection(context);
	}
}

template <typename Exec>
RIWO_WEBSOCKET_TAPI asio::awaitable<optional<std::tuple<error_code,basic_accept_result<Exec>>>,Exec>
co_upgrade(http::basic_service_context<Exec> *active_context, upgrade_options active_options)
{
	using result_t = basic_accept_result<Exec>;
	result_t result(active_context->get_executor());

	error_code adopt_error {};
	try {
		result.stream = basic_stream<Exec>(
			active_context->get_executor(), active_options.stream
		);
		if( active_options.handshake_timeout <= std::chrono::milliseconds::zero() )
		{
			close_upgrade_connection(*active_context);
			co_return std::tuple<error_code,result_t> {
				asio::error::timed_out, std::move(result)
			};
		}
		const auto deadline = std::chrono::steady_clock::now() +
			active_options.handshake_timeout;

		auto plan = make_server_upgrade_plan (
			*active_context, active_options, deadline, true
		);
		if( plan.accepted and active_options.async_request_validator )
		{
			auto invoke = [&active_options, &plan]() -> awaitable<upgrade_validation_result>
			{
				co_return co_await active_options.async_request_validator (
					plan.request
				);
			};
			auto validation_result = co_await asio::co_spawn (
				active_context->get_executor(), invoke(), asio::as_tuple(deferred)
			);
			auto &[exception, rejection] = validation_result;
			if( auto validation_error = exception_error(exception) )
			{
				reject_upgrade(plan,
					upgrade_rejection {
						.status = http::status::internal_server_error
					},
					validation_error
				);
				plan.preserve_error_on_write_failure = true;
			}
			else if( rejection )
			{
				reject_upgrade(plan, std::move(*rejection),
					make_error_code(errc::handshake_rejected)
				);
			}
		}
		if( plan.accepted and server_deadline_expired(deadline) )
			reject_upgrade(plan, asio::error::timed_out);

		if( plan.accepted and active_options.async_origin_validator )
		{
			optional<std::string> origin;
			if( auto it = plan.request.request_headers.find(http::header::origin);
				it != plan.request.request_headers.end() )
				origin = it->second.to_string();

			auto invoke = [&active_options, origin = std::move(origin)]
			() mutable -> awaitable<upgrade_validation_result>
			{
				co_return co_await active_options.async_origin_validator (
					std::move(origin)
				);
			};
			auto validation_result = co_await asio::co_spawn (
				active_context->get_executor(), invoke(), asio::as_tuple(deferred)
			);
			auto &[exception, rejection] = validation_result;
			if( auto validation_error = exception_error(exception) )
			{
				reject_upgrade(plan,
					upgrade_rejection {
						.status = http::status::internal_server_error
					},
					validation_error
				);
				plan.preserve_error_on_write_failure = true;
			}
			else if( rejection )
			{
				reject_upgrade(plan, std::move(*rejection),
					make_error_code(errc::handshake_rejected)
				);
			}
		}
		if( plan.accepted and server_deadline_expired(deadline) )
			reject_upgrade(plan, asio::error::timed_out);

		if( plan.accepted and active_options.async_subprotocol_selector )
		{
			auto invoke = [&active_options, &plan]() -> awaitable<optional<std::string>>
			{
				co_return co_await active_options.async_subprotocol_selector(
					plan.request, plan.opening.subprotocols);
			};
			auto selection_result = co_await asio::co_spawn (
				active_context->get_executor(), invoke(), asio::as_tuple(deferred)
			);
			auto &[exception, selected] = selection_result;

			if( exception_error(exception) )
				reject_selector_exception(plan, exception);
			else
				plan.response.subprotocol = std::move(selected);
		}
		else if( plan.accepted )
			select_server_subprotocol(plan, active_options);

		validate_server_subprotocol(plan, active_options, deadline);

		if( plan.accepted and active_options.async_extension_selector )
		{
			auto invoke = [&active_options, &plan]() -> awaitable<std::vector<extension>>
			{
				co_return co_await active_options.async_extension_selector(
					plan.request, plan.opening.extensions
				);
			};
			auto selection_result = co_await asio::co_spawn (
				active_context->get_executor(), invoke(), asio::as_tuple(deferred)
			);
			auto &[exception, selected] = selection_result;
			if( exception_error(exception) )
				reject_selector_exception(plan, exception);
			else
				plan.response.extensions = std::move(selected);
		}
		else if( plan.accepted )
			select_server_extensions(plan, active_options);

		validate_server_extensions(plan, active_options, deadline);
		finalize_server_upgrade_plan(plan, active_options, deadline);

		result.request = plan.request;
		if( plan.error == asio::error::timed_out or server_deadline_expired(deadline) )
		{
			close_upgrade_connection(*active_context);
			co_return std::tuple<error_code,result_t> {
				asio::error::timed_out, std::move(result)
			};
		}
		prepare_response(active_context->response(), plan);
		auto remaining = server_remaining_timeout(deadline);

		if( remaining <= std::chrono::milliseconds::zero() )
		{
			close_upgrade_connection(*active_context);
			co_return std::tuple<error_code,result_t> {
				asio::error::timed_out, std::move(result)
			};
		}
		auto [write_error, transferred] =
			co_await active_context->response().write (
				asio::buffer(plan.body), asio::as_tuple(asio::use_awaitable_t<Exec>{})
			);
		ignore_unused(transferred);
		if( write_error )
		{
			close_upgrade_connection(*active_context);
			co_return std::tuple<error_code,result_t> {
				plan.preserve_error_on_write_failure ?
					plan.error : write_error, std::move(result)
			};
		}
		if( server_deadline_expired(deadline) )
		{
			close_upgrade_connection(*active_context);
			co_return std::tuple<error_code,result_t> {
				asio::error::timed_out, std::move(result)
			};
		}
		if( not plan.accepted )
		{
			co_return std::tuple<error_code,result_t> {
				plan.error, std::move(result)
			};
		}
		auto pending = active_context->request().take_pending_data();
		auto connection = active_context->hand_over_connection();

		adopt_options adopt {
			.stream_role = role::server,
			.pending_data = std::vector<std::byte>(pending.size()),
			.negotiated_subprotocol = plan.response.subprotocol.value_or(""),
			.negotiated_extensions = plan.response.extensions,
		};
		if( not pending.empty() )
			std::memcpy(adopt.pending_data.data(), pending.data(), pending.size());

		result.handshake.subprotocol = adopt.negotiated_subprotocol;
		result.handshake.extensions = adopt.negotiated_extensions;
		result.stream.adopt(std::move(connection), std::move(adopt), adopt_error);

		co_return std::tuple<error_code,result_t> {
			adopt_error, std::move(result)
		};
	}
	catch(...)
	{
		close_upgrade_connection(*active_context);
		adopt_error = exception_error(std::current_exception());
	}
	co_return std::tuple<error_code,result_t> {
		adopt_error, std::move(result)
	};
}

template <typename Exec, typename Handler>
RIWO_WEBSOCKET_TAPI void start_upgrade
(http::basic_service_context<Exec> &context, upgrade_options options, Handler &&handler)
{
	using result_t = basic_accept_result<Exec>;
	auto exec = context.get_executor();

	using handler_t = std::remove_cvref_t<Handler>;
	auto fallback = [exec] {
		return result_t(exec);
	};
	using factory_t = decltype(fallback);
	riwo::detail::launch_awaitable(exec, co_upgrade(&context, std::move(options)),
		riwo::detail::awaitable_optional_tuple_io_handler<result_t,handler_t,decltype(exec),factory_t>(
			std::forward<Handler>(handler), exec, std::move(fallback)
		)
	);
}

} //namespace detail

template <core_concepts::exec Exec>
basic_accept_result<Exec>::basic_accept_result
(core_concepts::match_sched<executor_t> auto &&exec) :
	stream(std::forward<decltype(exec)>(exec))
{

}

template <core_concepts::exec Exec>
basic_accept_result<Exec>::basic_accept_result(stream_t value) :
	stream(std::move(value))
{

}

template <core_concepts::exec Exec>
basic_accept_result<Exec>::basic_accept_result
(stream_t value, request_info request_value, upgrade_result handshake_value) :
	stream(std::move(value)), request(std::move(request_value)),
	handshake(std::move(handshake_value))
{

}

template <http::concepts::any_exec_stream Stream>
class RIWO_WEBSOCKET_TAPI basic_server<Stream>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	enum class delivery_mode : uint8_t {
		unset, accept, handler
	};

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
			if( completed.exchange(true, std::memory_order_acq_rel) )
				return ;

			if( caller_slot.is_connected() )
				caller_slot.clear();
			try {
				completion(error, std::move(result));
			}
			catch(...) {
				forced_termination();
			}
		}
	};

	struct pending_handshake
	{
		explicit pending_handshake(const executor_t &exec) :
			timer(exec) {}

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
	impl(acceptor_wrap_t &&wrap, asio::any_io_executor service_exec, config_t config) :
		m_http_server(std::move(wrap), std::move(service_exec)),
		m_config(validate_config(std::move(config))) {}

	explicit impl(acceptor_wrap_t &&wrap, config_t config) :
		m_http_server(std::move(wrap)), m_config(validate_config(std::move(config))) {}

private:
	[[nodiscard]] static config_t validate_config(config_t config)
	{
		if( config.default_upgrade.stream.read_buffer_size == 0 or
			config.default_upgrade.stream.ping_interval < std::chrono::milliseconds::zero() )
		{
			system_error::loc_throw (
				make_system_error_code(std::errc::invalid_argument),
				"riwo::websocket::basic_server"
			);
		}
		return config;
	}

	[[nodiscard]] accept_result_t idle_result() noexcept {
		return accept_result_t(m_http_server.get_executor());
	}

	[[nodiscard]] bool enter_accept_mode()
	{
		bool install_handler = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_mode == delivery_mode::handler )
				return false;

			if( m_mode == delivery_mode::unset )
			{
				m_mode = delivery_mode::accept;
				install_handler = true;
			}
		}
		if( install_handler )
		{
			std::weak_ptr<impl> weak = this->shared_from_this();
			m_http_server.on_default([weak](context_t &context) -> awaitable<void>
			{
				if( auto self = weak.lock() )
					co_await self->serve_accept(context);
				else
					detail::close_upgrade_connection(context);
				co_return ;
			});
		}
		return true;
	}

	void enter_handler_mode()
	{
		std::lock_guard lock(m_mutex);
		if( m_mode == delivery_mode::accept )
		{
			logic_error::loc_throw (
				"riwo::websocket::basic_server: accept mode is already active"
			);
		}
		m_mode = delivery_mode::handler;
	}

	void post_timer_cancel(const std::shared_ptr<pending_handshake> &pending)
	{
		asio::post(m_http_server.get_executor(), [pending]
		{
			try {
				ignore_unused(pending->timer.cancel());
			}
			catch(...) {}
		});
	}

	[[nodiscard]] std::shared_ptr<pending_handshake> pop_pending_handshake_locked()
	{
		while(not m_pending_handshakes.empty())
		{
			auto pending = m_pending_handshakes.front();
			m_pending_handshakes.pop_front();

			if( not pending->queued )
				continue;

			pending->queued = false;
			return pending;
		}
		return {};
	}

	[[nodiscard]] std::shared_ptr<accept_waiter> pop_accept_locked()
	{
		while(not m_accepts.empty())
		{
			auto waiter = m_accepts.front();
			m_accepts.pop_front();

			if( waiter->cancelled.load(std::memory_order_acquire) or
				waiter->completed.load(std::memory_order_acquire) )
				continue;
			return waiter;
		}
		return {};
	}

	void enqueue_accept(const std::shared_ptr<accept_waiter> &waiter, bool front = false)
	{
		std::shared_ptr<pending_handshake> pending;
		bool abort = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_stopped or waiter->cancelled.load(std::memory_order_acquire) )
				abort = true;

			else if( (pending = pop_pending_handshake_locked()) )
				pending->waiter = waiter;

			else if( front )
				m_accepts.emplace_front(waiter);
			else
				m_accepts.emplace_back(waiter);
		}
		if( pending )
			post_timer_cancel(pending);

		else if( abort )
			waiter->complete(asio::error::operation_aborted, idle_result());
	}

	void cancel_waiter(const std::shared_ptr<accept_waiter> &waiter) noexcept
	{
		if( waiter->cancelled.exchange(true, std::memory_order_acq_rel) )
			return ;
		{
			std::lock_guard lock(m_mutex);
			auto it = std::find(m_accepts.begin(), m_accepts.end(), waiter);

			if( it != m_accepts.end() )
				m_accepts.erase(it);
		}
		waiter->cancellation.emit(asio::cancellation_type::all);
		waiter->complete(asio::error::operation_aborted, idle_result());
	}

	template <typename Handler>
	[[nodiscard]] std::shared_ptr<accept_waiter> make_waiter(upgrade_options_t options, Handler &&handler)
	{
		using handler_t = std::remove_cvref_t<Handler>;
		auto owned_handler = std::make_shared<handler_t>(
			std::forward<Handler>(handler)
		);
		auto waiter = std::make_shared<accept_waiter>();
		waiter->options = std::move(options);
		waiter->caller_slot = asio::get_associated_cancellation_slot(*owned_handler);

		auto completion_exec = asio::get_associated_executor(
			*owned_handler, m_http_server.get_executor()
		);
		auto allocator = asio::get_associated_allocator(*owned_handler);
		waiter->completion = [owned_handler, completion_exec, allocator]
		(error_code error, accept_result_t result) mutable
		{
			asio::post(completion_exec, asio::bind_allocator(allocator,
			[owned_handler, error, result = std::move(result)]() mutable {
				std::move(*owned_handler)(error, std::move(result));
			}));
		};
		if( waiter->caller_slot.is_connected() )
		{
			std::weak_ptr<impl> weak_self = this->shared_from_this();
			std::weak_ptr<accept_waiter> weak_waiter = waiter;

			waiter->caller_slot.assign (
			[weak_self, weak_waiter](asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto self = weak_self.lock() )
				{
					if( auto active = weak_waiter.lock() )
						self->cancel_waiter(active);
				}
			});
		}
		std::lock_guard lock(m_mutex);
		m_all_accepts.erase(std::remove_if (
			m_all_accepts.begin(), m_all_accepts.end(),
			[](const auto &item) {
				return item.expired();
			}),
			m_all_accepts.end()
		);
		m_all_accepts.emplace_back(waiter);
		return waiter;
	}

	[[nodiscard]] awaitable<acquisition> acquire_accept()
	{
		std::shared_ptr<pending_handshake> pending;
		{
			std::lock_guard lock(m_mutex);
			if( m_stopped )
				co_return acquisition {.stopped = true};

			if( auto waiter = pop_accept_locked() )
				co_return acquisition {.waiter = std::move(waiter)};

			const auto config = m_config;
			if( config.max_pending_handshakes == 0 or
				config.pending_handshake_timeout <= std::chrono::milliseconds::zero() or
				m_pending_handshakes.size() >= config.max_pending_handshakes )
			{
				co_return acquisition {
					.rejection_timeout = config.default_upgrade.handshake_timeout
				};
			}
			pending = std::make_shared<pending_handshake>(
				m_http_server.get_executor()
			);
			pending->timer.expires_after(config.pending_handshake_timeout);
			pending->rejection_timeout = config.default_upgrade.handshake_timeout;

			pending->queued = true;
			m_pending_handshakes.emplace_back(pending);
		}
		auto [timer_error] = co_await pending->timer.async_wait (
			asio::as_tuple(use_awaitable)
		);
		ignore_unused(timer_error);
		std::lock_guard lock(m_mutex);

		if( pending->waiter )
			co_return acquisition {.waiter = std::move(pending->waiter)};

		if( pending->queued )
		{
			auto it = std::find(m_pending_handshakes.begin(),
				m_pending_handshakes.end(), pending
			);
			if( it != m_pending_handshakes.end() )
				m_pending_handshakes.erase(it);
			pending->queued = false;
		}
		co_return acquisition {
			.rejection_timeout = pending->rejection_timeout,
			.stopped = pending->stopped or m_stopped
		};
	}

	awaitable<void> reject_unavailable(context_t &context, std::chrono::milliseconds timeout)
	{
		if( timeout <= std::chrono::milliseconds::zero() )
		{
			detail::close_upgrade_connection(context);
			co_return ;
		}
		constexpr std::string_view body =
			"WebSocket accept queue unavailable\n";

		context.response()
			.set_status(http::status::service_unavailable)
			.set_header(http::header::connection, "close");

		auto [error, transferred] = co_await context.response().write(
			asio::buffer(body), redirect_time(asio::as_tuple(use_awaitable), timeout)
		);
		ignore_unused(error, transferred);
		detail::close_upgrade_connection(context);
		co_return ;
	}

	awaitable<void> serve_accept(context_t &context)
	{
		if( not is_upgrade_request(context.request()) )
		{
			upgrade_options_t options;
			{
				std::lock_guard lock(m_mutex);
				options = m_config.default_upgrade;
			}
			auto upgrade_result = co_await websocket::upgrade (
				context, std::move(options), asio::as_tuple(use_awaitable)
			);
			ignore_unused(upgrade_result);
			co_return ;
		}
		auto acquired = co_await acquire_accept();
		if( acquired.stopped )
		{
			detail::close_upgrade_connection(context);
			co_return ;
		}
		if( not acquired.waiter )
		{
			co_await reject_unavailable(context, acquired.rejection_timeout);
			co_return ;
		}
		auto waiter = std::move(acquired.waiter);
		if( waiter->cancelled.load(std::memory_order_acquire) )
		{
			waiter->complete(asio::error::operation_aborted, idle_result());
			co_await reject_unavailable(context,
				config_snapshot().default_upgrade.handshake_timeout
			);
			co_return ;
		}
		auto upgrade_result = co_await websocket::upgrade (
			context, waiter->options, asio::bind_cancellation_slot (
				waiter->cancellation.slot(), asio::as_tuple(use_awaitable)
			)
		);
		auto &[error, result] = upgrade_result;
		if( not error )
		{
			waiter->complete({}, std::move(result));
			co_return ;
		}
		if( waiter->cancelled.load(std::memory_order_acquire) or stopped() )
		{
			waiter->complete(asio::error::operation_aborted, std::move(result));
			co_return ;
		}
		// A malformed/rejected request must not consume a pending accept. The
		// same options and FIFO position are retained for the next request.
		enqueue_accept(waiter, true);
		co_return ;
	}

public:
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
		config = validate_config(std::move(config));
		std::lock_guard lock(m_mutex);
		m_config = std::move(config);
	}

	template <typename Token>
	[[nodiscard]] auto async_accept(upgrade_options_t options, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));

		auto self = this->shared_from_this();
		return asio::async_initiate<token_t,void(error_code,accept_result_t)>(
		[self = std::move(self), options = std::move(options)](auto completion_handler) mutable
		{
			auto waiter = self->make_waiter (
				std::move(options), std::move(completion_handler)
			);
			if( not self->enter_accept_mode() )
			{
				waiter->complete (
					make_system_error_code(std::errc::operation_not_supported),
					self->idle_result()
				);
				return ;
			}
			self->enqueue_accept(waiter);
		},
		completion_token);
	}

	[[nodiscard]] accept_result_t accept_sync(upgrade_options_t options, error_code &error) noexcept
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
					state->result = std::make_unique<accept_result_t>(std::move(result));
				}
				state->changed.notify_one();
			});
			waiter->completion = [state](error_code accept_error, accept_result_t result) mutable
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
			{
				waiter->complete (
					make_system_error_code(std::errc::operation_not_supported),
					idle_result()
				);
			}
			else
				enqueue_accept(waiter);

			std::unique_lock lock(state->mutex);
			state->changed.wait(lock, [&] { return static_cast<bool>(state->result); });

			error = state->error;
			return std::move(*state->result);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return idle_result();
	}

	template <typename Func>
	void bind_connection
	(const path_opt_token_t &path_rules, Func &&func, optional<upgrade_options_t> options)
	{
		enter_handler_mode();
		auto selected = options.value_or(config_snapshot().default_upgrade);

		auto handler = std::make_shared<std::remove_cvref_t<Func>>(
			std::forward<Func>(func)
		);
		m_http_server.template on_request<http::method::get>(path_rules,
		[handler, selected = std::move(selected)](context_t &context) mutable -> awaitable<void>
		{
			auto upgrade_result = co_await websocket::upgrade (
				context, selected, asio::as_tuple(use_awaitable)
			);
			auto &[error, result] = upgrade_result;

			if( not error )
				co_await (*handler)(std::move(result));
			co_return ;
		});
	}

	template <typename Func>
	void bind_default(Func &&func, optional<upgrade_options_t> options)
	{
		enter_handler_mode();
		auto selected = options.value_or(config_snapshot().default_upgrade);

		auto handler = std::make_shared<std::remove_cvref_t<Func>>(
			std::forward<Func>(func)
		);
		m_http_server.on_default (
		[handler, selected = std::move(selected)](context_t &context) mutable -> awaitable<void>
		{
			auto upgrade_result = co_await websocket::upgrade (
				context, selected, asio::as_tuple(use_awaitable)
			);
			auto &[error, result] = upgrade_result;

			if( not error )
				co_await (*handler)(std::move(result));
			co_return ;
		});
	}

	void cancel_all() noexcept
	{
		std::vector<std::shared_ptr<accept_waiter>> accepts;
		std::deque<std::shared_ptr<pending_handshake>> handshakes;
		{
			std::lock_guard lock(m_mutex);
			m_stopped = true;

			for(auto &item : m_all_accepts)
			{
				if( auto waiter = item.lock(); waiter and
					not waiter->completed.load(std::memory_order_acquire) )
					accepts.emplace_back(std::move(waiter));
			}
			m_accepts.clear();
			m_all_accepts.clear();

			handshakes.swap(m_pending_handshakes);
			for(auto &pending : handshakes)
			{
				pending->queued = false;
				pending->stopped = true;
			}
		}
		for(auto &waiter : accepts)
		{
			waiter->cancelled.store(true, std::memory_order_release);
			waiter->cancellation.emit(asio::cancellation_type::all);
			waiter->complete(asio::error::operation_aborted, idle_result());
		}
		for(auto &pending : handshakes)
			post_timer_cancel(pending);
	}

	void stop() noexcept
	{
		cancel_all();
		m_http_server.stop();
	}

public:
	http_server_t m_http_server;
	mutable std::mutex m_mutex;

	config_t m_config {};
	delivery_mode m_mode = delivery_mode::unset;
	bool m_stopped = false;

	std::deque<std::shared_ptr<accept_waiter>> m_accepts {};
	std::deque<std::shared_ptr<pending_handshake>> m_pending_handshakes {};
	std::vector<std::weak_ptr<accept_waiter>> m_all_accepts {};
};

template <http::concepts::any_exec_stream Stream>
basic_server<Stream>::basic_server(acceptor_wrap_t &&wrap,
	core_concepts::sched auto &&service_exec, config_t config) :
	m_impl(std::make_shared<impl>(std::move(wrap),
		asio::any_io_executor(get_executor_helper(std::forward<decltype(service_exec)>(service_exec))),
		std::move(config)
	))
{

}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream>::basic_server(acceptor_wrap_t &&wrap, config_t config) :
	m_impl(std::make_shared<impl>(std::move(wrap), std::move(config)))
{

}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream>::~basic_server()
{
	if( m_impl )
		m_impl->stop();
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::bind(endpoint_wrapper_t endpoint)
{
	m_impl->m_http_server.bind(std::move(endpoint));
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::bind
(endpoint_wrapper_t endpoint, error_code &error) noexcept
{
	m_impl->m_http_server.bind(std::move(endpoint), error);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(size_t max)
{
	m_impl->m_http_server.start(max);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(size_t max, error_code &error) noexcept
{
	m_impl->m_http_server.start(max, error);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start(error_code &error) noexcept
{
	m_impl->m_http_server.start(error);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto &&service_exec, size_t max)
{
	m_impl->m_http_server.start (
		std::forward<decltype(service_exec)>(service_exec), max
	);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto &&service_exec, size_t max, error_code &error) noexcept
{
	m_impl->m_http_server.start (
		std::forward<decltype(service_exec)>(service_exec), max, error
	);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::start
(core_concepts::sched auto &&service_exec, error_code &error) noexcept
{
	m_impl->m_http_server.start (
		std::forward<decltype(service_exec)>(service_exec), error
	);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
template <typename Token>
auto basic_server<Stream>::accept(Token &&token)
	requires accept_token_v<Token>
{
	return accept(config().default_upgrade, std::forward<Token>(token));
}

template <http::concepts::any_exec_stream Stream>
template <typename Token>
auto basic_server<Stream>::accept(upgrade_options_t options, Token &&token)
	requires accept_token_v<Token>
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
		if( error )
		{
			system_error::loc_throw(error,
				"riwo::websocket::basic_server::accept"
			);
		}
		return result;
	}
	else
	{
		return m_impl->async_accept(std::move(options),
			std::forward<Token>(token)
		);
	}
}

template <http::concepts::any_exec_stream Stream>
size_t basic_server<Stream>::pending_accept_count() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_accepts.size();
}

template <http::concepts::any_exec_stream Stream>
size_t basic_server<Stream>::pending_handshake_count() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_pending_handshakes.size();
}

template <http::concepts::any_exec_stream Stream>
template <typename Func>
basic_server<Stream> &basic_server<Stream>::on_connection
(const path_opt_token_t &path_rules, Func &&func, optional<upgrade_options_t> options)
	requires connection_handler_v<Func>
{
	m_impl->bind_connection (
		path_rules, std::forward<Func>(func), std::move(options)
	);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
template <typename Func>
basic_server<Stream> &basic_server<Stream>::on_default(Func &&func, optional<upgrade_options_t> options)
	requires connection_handler_v<Func>
{
	m_impl->bind_default(std::forward<Func>(func), std::move(options));
	return *this;
}

template <http::concepts::any_exec_stream Stream>
template <core_concepts::text_p<char> Text>
basic_server<Stream> &basic_server<Stream>::unbound_connection(const Text &path_rule)
{
	if( auto rule = strtls::to_string(path_rule); rule.empty() )
	{
		m_impl->m_http_server.on_default (
			[](context_t&) -> awaitable<void> { co_return ; }
		);
	}
	else
		m_impl->m_http_server.unbound_request(rule);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::on_server_error(server_error_handler_t func)
{
	m_impl->m_http_server.on_server_error(std::move(func));
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::on_service_error(service_error_handler_t func)
{
	m_impl->m_http_server.on_service_error(std::move(func));
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::unbound_server_error()
{
	m_impl->m_http_server.unbound_server_error();
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::unbound_service_error()
{
	m_impl->m_http_server.unbound_service_error();
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::set_config(const config_t &config)
{
	m_impl->set_config(config);
	return *this;
}

template <http::concepts::any_exec_stream Stream>
auto basic_server<Stream>::config() const -> config_t
{
	return m_impl->config_snapshot();
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::cancel() noexcept
{
	m_impl->stop();
	return *this;
}

template <http::concepts::any_exec_stream Stream>
basic_server<Stream> &basic_server<Stream>::stop() noexcept
{
	m_impl->stop();
	return *this;
}

template <http::concepts::any_exec_stream Stream>
auto basic_server<Stream>::http_server() const noexcept -> const http_server_t&
{
	return m_impl->m_http_server;
}

template <http::concepts::any_exec_stream Stream>
auto basic_server<Stream>::http_server() noexcept -> http_server_t&
{
	return m_impl->m_http_server;
}

template <http::concepts::any_exec_stream Stream>
auto basic_server<Stream>::get_executor() noexcept -> executor_t
{
	return m_impl->m_http_server.get_executor();
}

template <core_concepts::exec Exec>
bool is_upgrade_request(const http::basic_request<Exec> &request) noexcept
{
	if( not request.is_upgrade() )
		return false;

	auto protocol = http::upgrade_protocol(request.headers());
	if( not protocol )
		return false;

	std::string_view value(*protocol);
	while(not value.empty() and (value.front() == ' ' or value.front() == '\t'))
		value.remove_prefix(1);

	while(not value.empty() and (value.back() == ' ' or value.back() == '\t'))
		value.remove_suffix(1);

	return detail::server_ascii_equal_case_insensitive(value, "websocket");
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http::basic_service_context<Exec> &context, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_accept_result<Exec>>
{
	return upgrade(context, upgrade_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto upgrade(http::basic_service_context<Exec> &context, upgrade_options options, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_accept_result<Exec>>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		basic_accept_result<Exec> result(context.get_executor());
		auto adapted_error = adapt_error_code(token);

		detail::upgrade_sync(context, std::move(options), result, adapted_error.get());
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		basic_accept_result<Exec> result(context.get_executor());
		error_code error;

		detail::upgrade_sync(context, std::move(options), result, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::upgrade");
		return result;
	}
	else
	{
		const auto timeout = options.handshake_timeout;
		return detail::initiate_handshake_io<basic_accept_result<Exec>>(
			context.get_executor(),
			[&context, options = std::move(options)]<typename Handler>(Handler &&handler) mutable
			{
				detail::start_upgrade(context, std::move(options),
					std::forward<Handler>(handler)
				);
			},
			timeout, nullopt,
			unbound_redirect_time(std::forward<Token>(token))
		);
	}
}

} //namespace riwo::websocket

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_WEBSOCKET_DETAIL_SERVER_H
