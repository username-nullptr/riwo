// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_CLIENT_H
#define RIWO_WEBSOCKET_DETAIL_CLIENT_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <riwo/websocket/protocol/handshake.h>
#include <riwo/http/client/detail/proxy.h>

#include <riwo/websocket/detail/permessage_deflate.h>
#include <riwo/websocket/detail/secure_random.h>
#include <riwo/websocket/detail/handshake_io.h>

namespace riwo::websocket { namespace detail
{

constexpr std::chrono::milliseconds default_handshake_timeout {30000};

[[nodiscard]] RIWO_WEBSOCKET_API
bool ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
bool websocket_owned_request_header(std::string_view name) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<url> canonical_websocket_url(const url &endpoint) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
error_code validate_open_request(connect_request &request, const stream_config &stream) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<url> http_transport_url(const url &endpoint) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
uint16_t websocket_effective_port(const url &value) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
bool same_websocket_origin(const url &lhs, const url &rhs) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
bool websocket_redirect_status(http::status_enum status) noexcept;

template <typename Exec>
RIWO_WEBSOCKET_TAPI void close_reply_connection
(const std::shared_ptr<http::basic_reply<Exec>> &reply) noexcept
{
	if( not reply or not reply->lease().is_valid() )
		return ;

	if( auto connection = reply->lease().take() )
	{
		ignore_unused(connection->cancel());
		ignore_unused(connection->close());
	}
}

template <typename ConnectionPtr>
[[nodiscard]] RIWO_WEBSOCKET_TAPI error_code configure_client_connection
(const ConnectionPtr &connection, optional<bool> no_delay) noexcept
{
	if( not connection )
		return make_system_error_code(std::errc::not_connected);

	if( not no_delay )
		return {};

	http::tcp_socket_options options;
	options.no_delay = *no_delay;

	auto configured = connection->set_options(options);
	return configured ? error_code{} : configured.error();
}

[[nodiscard]] RIWO_WEBSOCKET_API
std::vector<std::byte> pending_bytes(const std::string &pending);

[[nodiscard]] RIWO_WEBSOCKET_API
std::chrono::milliseconds remaining_timeout(std::chrono::steady_clock::time_point deadline) noexcept;

struct open_http_request
{
	opening_request opening {};
	url transport {};
	http::request_arg options {};
	std::optional<http::proxy_t> proxy {};
};

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<open_http_request>
prepare_open_http_request(const connect_request &request, const url &endpoint,
	const http::request_arg &base_options, bool inherit_global_proxy) noexcept;

enum class open_response_action : uint8_t {
	accept, redirect, reject,
};

struct open_response_decision
{
	open_response_action action = open_response_action::reject;
	error_code error {};
	url redirect_endpoint {};
	opening_response response {};
	bool clear_credentials = false;
	bool close_connection = false;
	bool record_diagnostics = false;
};

[[nodiscard]] RIWO_WEBSOCKET_API open_response_decision
evaluate_open_response(http::status_enum status, const http::headers &headers,
	const opening_request &opening, const connect_request &request,
	const url &endpoint, const stream_config &stream, size_t redirects) noexcept;

template <typename Exec, http::version_enum Version>
void open_sync(http::basic_client<Exec,Version> &http_client, connect_request request,
	basic_open_diagnostics<Exec> *diagnostics, stream_config stream_config_value,
	optional<bool> no_delay, std::chrono::milliseconds timeout,
	basic_stream<Exec> &result, error_code &error) noexcept
{
	using context_ptr = http::basic_client<Exec,Version>::
		template context_ptr<http::method::get>;

	context_ptr context;
	try {
		error = validate_open_request(request, stream_config_value);
		if( error )
			return ;

		if( diagnostics )
			diagnostics->endpoint = request.endpoint;

		if( timeout <= std::chrono::milliseconds::zero() )
		{
			error = asio::error::timed_out;
			return ;
		}
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		auto base_options = request.request_options;
		auto endpoint = request.endpoint;
		size_t redirects = 0;
		for(;;)
		{
			if( remaining_timeout(deadline) <= std::chrono::milliseconds::zero() )
			{
				error = asio::error::timed_out;
				return ;
			}
			const bool inherit_global_proxy =
				std::holds_alternative<use_global_proxy_t>(http_client.config().default_proxy);
			auto prepared = prepare_open_http_request(request, endpoint,
				base_options, inherit_global_proxy);
			if( not prepared )
			{
				error = prepared.error();
				return ;
			}
			auto opening = std::move(prepared->opening);
			typename http::basic_client<Exec,Version>::req_info info(
				std::move(prepared->transport), std::move(prepared->options));
			info.proxy = std::move(prepared->proxy);
			info.max_redirects = 0;
			info.auto_decompression = false;
			context = http_client.request_get(std::move(info), error);
			if( error )
				return ;

			if( remaining_timeout(deadline) <= std::chrono::milliseconds::zero() )
			{
				context->cancel();
				close_reply_connection(context->reply());
				error = asio::error::timed_out;
				return ;
			}
			ignore_unused(context->wait_reply(error));
			if( error )
				return ;

			if( remaining_timeout(deadline) <= std::chrono::milliseconds::zero() )
			{
				context->cancel();
				close_reply_connection(context->reply());
				error = asio::error::timed_out;
				return ;
			}
			auto reply = context->reply();
			const auto status = reply->status();

			auto decision = evaluate_open_response(status, reply->headers(), opening,
				request, endpoint, stream_config_value, redirects);
			if( decision.record_diagnostics and diagnostics )
			{
				diagnostics->endpoint = endpoint;
				diagnostics->reply = reply;
			}
			if( decision.action == open_response_action::redirect )
			{
				if( decision.clear_credentials )
				{
					base_options.unset_header(http::header::authorization);
					base_options.unset_header("Cookie");
					base_options.cookies().clear();
				}
				endpoint = std::move(decision.redirect_endpoint);
				context.reset();
				++redirects;
				continue;
			}
			if( decision.action == open_response_action::reject )
			{
				error = decision.error;
				if( decision.close_connection )
					close_reply_connection(reply);
				return ;
			}
			auto pending = pending_bytes(reply->take_pending_data());
			auto connection = reply->lease().take();

			error = configure_client_connection(connection, no_delay);
			if( error )
			{
				if( connection )
					ignore_unused(connection->close());
				return ;
			}
			adopt_options adopt {
				.stream_role = role::client,
				.pending_data = std::move(pending),
				.negotiated_subprotocol = decision.response.subprotocol.value_or(""),
				.negotiated_extensions = std::move(decision.response.extensions),
			};
			result.adopt(std::move(connection), std::move(adopt), error);
			return ;
		}
	}
	catch(...)
	{
		error = exception_error(std::current_exception());
		if( context )
			close_reply_connection(context->reply());
	}
}

template <typename Exec, http::version_enum Version>
asio::awaitable<optional<std::tuple<error_code,basic_stream<Exec>>>,Exec>
co_open(http::basic_client<Exec,Version> *client, connect_request active_request,
	basic_open_diagnostics<Exec> *active_diagnostics, stream_config active_stream_config,
	optional<bool> active_no_delay, std::chrono::milliseconds active_timeout)
{
	using result_t = basic_stream<Exec>;
	using context_ptr = http::basic_client<Exec,Version>::
		template context_ptr<http::method::get>;

	result_t result(client->get_executor(), active_stream_config);
	context_ptr context;
	try {
		if( auto error = validate_open_request(active_request, active_stream_config) )
		{
			co_return std::tuple<error_code,result_t> {
				error, std::move(result)
			};
		}
		if( active_diagnostics )
			active_diagnostics->endpoint = active_request.endpoint;

		if( active_timeout <= std::chrono::milliseconds::zero() )
		{
			co_return std::tuple<error_code,result_t> {
				asio::error::timed_out, std::move(result)
			};
		}
		const auto deadline = std::chrono::steady_clock::now() + active_timeout;
		auto base_options = active_request.request_options;
		auto endpoint = active_request.endpoint;
		size_t redirects = 0;
		for(;;)
		{
			auto remaining = remaining_timeout(deadline);
			if( remaining <= std::chrono::milliseconds::zero() )
			{
				co_return std::tuple<error_code,result_t> {
					asio::error::timed_out, std::move(result)
				};
			}
			const bool inherit_global_proxy =
				std::holds_alternative<use_global_proxy_t>(client->config().default_proxy);
			auto prepared = prepare_open_http_request(active_request, endpoint,
				base_options, inherit_global_proxy);
			if( not prepared )
			{
				co_return std::tuple<error_code,result_t> {
					prepared.error(), std::move(result)
				};
			}
			auto opening = std::move(prepared->opening);
			using req_info_t = http::basic_client<Exec,Version>::req_info;

			req_info_t info (
				std::move(prepared->transport), std::move(prepared->options)
			);
			info.proxy = std::move(prepared->proxy);
			info.auto_decompression = false;
			info.max_redirects = 0;

			auto request_result = co_await http::detail::client_access::request
				<http::method::get>(*client, std::move(info));

			auto &[request_error, next_context] = request_result;
			if( request_error )
			{
				co_return std::tuple<error_code,result_t> {
					request_error, std::move(result)
				};
			}
			context = std::move(next_context);
			remaining = remaining_timeout(deadline);

			if( remaining <= std::chrono::milliseconds::zero() )
			{
				context->cancel();
				close_reply_connection(context->reply());

				co_return std::tuple<error_code,result_t> {
					asio::error::timed_out, std::move(result)
				};
			}
			auto reply_result = co_await http::detail::reply_access::wait(*context->reply());
			auto &[reply_error, status] = reply_result;

			if( reply_error )
			{
				context->cancel();
				close_reply_connection(context->reply());

				co_return std::tuple<error_code,result_t> {
					reply_error, std::move(result)
				};
			}
			auto reply = context->reply();
			auto decision = evaluate_open_response(status, reply->headers(), opening,
				active_request, endpoint, active_stream_config, redirects);
			if( decision.record_diagnostics and active_diagnostics )
			{
				active_diagnostics->endpoint = endpoint;
				active_diagnostics->reply = reply;
			}
			if( decision.action == open_response_action::redirect )
			{
				if( decision.clear_credentials )
				{
					base_options.unset_header(http::header::authorization);
					base_options.unset_header("Cookie");
					base_options.cookies().clear();
				}
				endpoint = std::move(decision.redirect_endpoint);
				context.reset();
				++redirects;
				continue;
			}
			if( decision.action == open_response_action::reject )
			{
				if( decision.close_connection )
					close_reply_connection(reply);
				co_return std::tuple<error_code,result_t> {
					decision.error, std::move(result)
				};
			}
			auto pending = pending_bytes(reply->take_pending_data());
			auto connection = reply->lease().take();

			auto option_error = configure_client_connection (
				connection, active_no_delay
			);
			if( option_error )
			{
				if( connection )
					ignore_unused(connection->close());
				co_return std::tuple<error_code,result_t> {
					option_error, std::move(result)
				};
			}
			adopt_options adopt {
				.stream_role = role::client,
				.pending_data = std::move(pending),
				.negotiated_subprotocol = decision.response.subprotocol.value_or(""),
				.negotiated_extensions = std::move(decision.response.extensions),
			};
			error_code adopt_error;
			result.adopt(std::move(connection), std::move(adopt), adopt_error);

			co_return std::tuple<error_code,result_t> {
				adopt_error, std::move(result)
			};
		}
	}
	catch(...)
	{
		if( context )
			close_reply_connection(context->reply());

		co_return std::tuple<error_code,result_t> {
			exception_error(std::current_exception()), std::move(result)
		};
	}
}

template <typename Exec, http::version_enum Version, typename Handler>
void start_open(http::basic_client<Exec,Version> &http_client, connect_request request,
	basic_open_diagnostics<Exec> *diagnostics, stream_config stream_config_value,
	optional<bool> no_delay, std::chrono::milliseconds timeout, Handler &&handler)
{
	using result_t = basic_stream<Exec>;
	auto exec = http_client.get_executor();
	using handler_t = std::remove_cvref_t<Handler>;
	auto fallback = [exec, stream_config_value] {
		return result_t(exec, stream_config_value);
	};
	using factory_t = decltype(fallback);
	riwo::detail::launch_awaitable(exec,
		co_open(&http_client, std::move(request), diagnostics,
			stream_config_value, no_delay, timeout),
		riwo::detail::awaitable_optional_tuple_io_handler<
			result_t,handler_t,decltype(exec),factory_t>(
				std::forward<Handler>(handler), exec, std::move(fallback)));
}

} //namespace detail

template <core_concepts::exec Exec>
class RIWO_WEBSOCKET_TAPI basic_client<Exec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	struct open_operation : std::enable_shared_from_this<open_operation>
	{
		using complete_fn_t = void (*)(open_operation&, error_code, stream_t);

		open_operation(std::shared_ptr<impl> owner, complete_fn_t complete_fn) :
			owner(std::move(owner)), complete_fn(complete_fn) {}

		void complete(error_code error, stream_t stream)
		{
			complete_fn(*this, error, std::move(stream));
		}

		asio::cancellation_signal cancellation {};
		std::atomic_bool finished {false};
		std::shared_ptr<impl> owner;
		complete_fn_t complete_fn;
	};

	template <typename Handler>
	struct open_handler_operation final : open_operation
	{
		using self_t = open_handler_operation<Handler>;
		using executor_type = asio::associated_executor_t<Handler,executor_t>;
		using allocator_type = asio::associated_allocator_t<Handler>;
		using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

		open_handler_operation(std::shared_ptr<impl> owner, Handler handler,
			const executor_t &exec) :
			open_operation(std::move(owner), &self_t::complete_handler),
			handler(std::move(handler)),
			executor(asio::get_associated_executor(this->handler, exec)),
			allocator(asio::get_associated_allocator(this->handler)),
			slot(asio::get_associated_cancellation_slot(this->handler)) {}

		void arm_cancellation(const std::shared_ptr<open_operation> &operation)
		{
			if( not slot.is_connected() )
				return ;

			std::weak_ptr<open_operation> weak_operation = operation;
			slot.assign([weak_operation](asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto active = weak_operation.lock() )
					active->cancellation.emit(type);
			});
		}

		static void complete_handler(open_operation &base, error_code error,
			stream_t stream)
		{
			auto &self = static_cast<self_t&>(base);
			if( self.slot.is_connected() )
				self.slot.clear();

			auto operation = self.shared_from_this();
			self.owner->release_operation(operation);
			auto handler = std::move(self.handler);
			asio::post(self.executor, asio::bind_allocator(self.allocator,
				[handler = std::move(handler), error,
				 stream = std::move(stream)]() mutable {
					std::move(handler)(error, std::move(stream));
				}));
		}

		Handler handler;
		executor_type executor;
		allocator_type allocator;
		cancellation_slot_type slot;
	};

	struct open_completion_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<open_operation> operation;

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept
		{
			return operation->cancellation.slot();
		}

		void operator()(error_code error, stream_t stream)
		{
			operation->complete(error, std::move(stream));
		}
	};

public:
	impl() requires core_concepts::match_sched<io_executor_t,executor_t> :
		m_http_client(), m_config(validate_config({})) {}

	explicit impl(const config_t &config) requires
		core_concepts::match_sched<io_executor_t,executor_t> :
		m_http_client(), m_config(validate_config(config)) {}

	explicit impl(executor_t exec, const config_t &config) :
		m_http_client(std::move(exec)), m_config(validate_config(config)) {}

	explicit impl(http_client_t &&http_client, const config_t &config) :
		m_http_client(std::move(http_client)), m_config(validate_config(config)) {}

private:
	[[nodiscard]] static config_t validate_config(config_t config)
	{
		if( config.stream.read_buffer_size == 0 or
			config.stream.ping_interval < std::chrono::milliseconds::zero() )
		{
			system_error::loc_throw (
				make_system_error_code(std::errc::invalid_argument),
				"riwo::websocket::basic_client"
			);
		}
		return config;
	}

	void prepare_request(connect_request_t &request) const
	{
		if( not request.proxy )
			request.proxy = m_config.default_proxy;

		if( not request.stream_options )
			request.stream_options = m_config.stream;

		if( not request.handshake_timeout )
			request.handshake_timeout = m_config.handshake_timeout;
	}

	void retain_operation(const std::shared_ptr<open_operation> &operation)
	{
		std::lock_guard lock(m_mutex);
		m_operations.erase (
			std::remove_if (
				m_operations.begin(), m_operations.end(),
				[](const auto &item) { return item.expired(); }
			),
			m_operations.end()
		);
		m_operations.emplace_back(operation);
		m_pending_open_count.fetch_add(1, std::memory_order_relaxed);
	}

	void release_operation(const std::shared_ptr<open_operation> &operation) noexcept
	{
		if( operation->finished.exchange(true, std::memory_order_acq_rel) )
			return ;

		m_pending_open_count.fetch_sub(1, std::memory_order_relaxed);
		std::lock_guard lock(m_mutex);

		m_operations.erase(std::remove_if (
			m_operations.begin(), m_operations.end(),
			[&](const auto &item)
			{
				auto value = item.lock();
				return not value or value == operation;
			}),
			m_operations.end()
		);
	}

public:
	template <typename Token>
	[[nodiscard]] auto async_open
	(connect_request_t request, diagnostics_t *diagnostics, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));

		prepare_request(request);
		auto self = this->shared_from_this();

		return asio::async_initiate<token_t,void(error_code,stream_t)>(
		[self = std::move(self), request = std::move(request), diagnostics]
		<typename Handle>(Handle completion_handler) mutable
		{
			using operation_t = open_handler_operation<Handle>;
			auto operation = std::make_shared<operation_t>(self,
				std::move(completion_handler), self->m_http_client.get_executor());
			self->retain_operation(operation);
			operation->arm_cancellation(operation);

			const auto stream_options = *request.stream_options;
			const auto timeout = *request.handshake_timeout;
			const auto no_delay = self->m_config.no_delay;

			detail::start_handshake_io<stream_t>(self->m_http_client.get_executor(),
				[client = &self->m_http_client, request = std::move(request), diagnostics,
				 stream_options, no_delay, timeout]<typename Handler>(Handler &&handler) mutable
				{
					detail::start_open(*client, std::move(request), diagnostics,
						stream_options, no_delay, timeout,
						std::forward<Handler>(handler)
					);
				},
				detail::effective_handshake_timeout(timeout),
				stream_options,
				open_completion_handler{std::move(operation)}
			);
		},
		completion_token);
	}

	[[nodiscard]] stream_t open_sync
	(connect_request_t request, diagnostics_t *diagnostics, error_code &error) noexcept
	{
		try {
			prepare_request(request);
			m_pending_open_count.fetch_add(1, std::memory_order_relaxed);

			struct counter_guard
			{
				std::atomic_size_t &counter;
				~counter_guard() {
					counter.fetch_sub(1, std::memory_order_relaxed);
				}
			}
			guard {m_pending_open_count};

			const auto stream_options = *request.stream_options;
			const auto timeout = *request.handshake_timeout;
			stream_t result(m_http_client.get_executor(), stream_options);

			detail::open_sync(m_http_client, std::move(request), diagnostics,
				stream_options, m_config.no_delay, timeout, result, error
			);
			return result;
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return stream_t(m_http_client.get_executor(), m_config.stream);
	}

	void cancel() noexcept
	{
		std::vector<std::shared_ptr<open_operation>> operations;
		{
			std::lock_guard lock(m_mutex);
			operations.reserve(m_operations.size());

			for(auto &item : m_operations)
			{
				if( auto operation = item.lock() )
					operations.emplace_back(std::move(operation));
			}
		}
		for(auto &operation : operations)
			operation->cancellation.emit(asio::cancellation_type::all);
	}

public:
	http_client_t m_http_client;
	config_t m_config {};

	std::atomic_size_t m_pending_open_count {0};
	std::mutex m_mutex;

	std::vector <
		std::weak_ptr<open_operation>
	> m_operations {};
};

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client() requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	m_impl(std::make_shared<impl>())
{

}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(config_t config) requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	m_impl(std::make_shared<impl>(std::move(config)))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&exec, config_t config) requires
(not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and core_concepts::match_sched<Exec0,executor_t>) :
	m_impl(std::make_shared<impl>(
		executor_t(get_executor_helper(std::forward<Exec0>(exec))),
		std::move(config)))
{

}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(http_client_t &&http_client, config_t config) :
	m_impl(std::make_shared<impl>(std::move(http_client), std::move(config)))
{

}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(basic_client &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

template <core_concepts::exec Exec>
basic_client<Exec> &basic_client<Exec>::operator=(basic_client &&other) noexcept
{
	m_impl = std::move(other.m_impl);
	return *this;
}

template <core_concepts::exec Exec>
basic_client<Exec>::~basic_client() = default;

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request, Token &&token)
	requires open_token_v<Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->open_sync(std::move(request), nullptr, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->open_sync(std::move(request), nullptr, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_client::open");
		return result;
	}
	else
	{
		return m_impl->async_open(std::move(request), nullptr,
			std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request, diagnostics_t &diagnostics, Token &&token)
	requires open_token_v<Token>
{
	diagnostics.endpoint = request.endpoint;
	diagnostics.reply.reset();

	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->open_sync(std::move(request), &diagnostics, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->open_sync(std::move(request), &diagnostics, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_client::open");
		return result;
	}
	else
	{
		return m_impl->async_open(std::move(request), &diagnostics,
			std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, Token &&token)
	requires open_token_v<Token>
{
	return open(connect_request_t(std::move(endpoint)), std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, diagnostics_t &diagnostics, Token &&token)
	requires open_token_v<Token>
{
	return open(connect_request_t(std::move(endpoint)), diagnostics,
		std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
std::shared_ptr<http::cookie_jar> basic_client<Exec>::cookie_store() noexcept
{
	return m_impl->m_http_client.cookie_store();
}

template <core_concepts::exec Exec>
size_t basic_client<Exec>::pending_open_count() const noexcept
{
	return m_impl->m_pending_open_count.load(std::memory_order_relaxed);
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::config() const -> config_t
{
	return m_impl->m_config;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() const noexcept -> const http_client_t&
{
	return m_impl->m_http_client;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() noexcept -> http_client_t&
{
	return m_impl->m_http_client;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::get_executor() const noexcept -> executor_t
{
	return m_impl->m_http_client.get_executor();
}

template <core_concepts::exec Exec>
basic_client<Exec> &basic_client<Exec>::cancel() noexcept
{
	m_impl->cancel();
	return *this;
}

template <core_concepts::exec Exec, http::version_enum Version, typename Token>
auto open(http::basic_client<Exec,Version> &http_client, connect_request request, Token &&token)
	requires (Version == http::version::v11) and
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>
{
	const auto stream_options = request.stream_options.value_or(stream_config{});
	const auto timeout = request.handshake_timeout.value_or(detail::default_handshake_timeout);

	if constexpr( is_error_code_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		auto adapted_error = adapt_error_code(token);

		detail::open_sync(http_client, std::move(request),
			static_cast<basic_open_diagnostics<Exec>*>(nullptr),
			stream_options, nullopt, timeout, result, adapted_error.get()
		);
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		error_code error;

		detail::open_sync(http_client, std::move(request),
			static_cast<basic_open_diagnostics<Exec>*>(nullptr),
			stream_options, nullopt, timeout, result, error
		);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::open");
		return result;
	}
	else
	{
		return detail::initiate_handshake_io<basic_stream<Exec>>(http_client.get_executor(),
		[&http_client, request = std::move(request), stream_options, timeout]
		<typename Handler>(Handler &&handler) mutable
		{
			detail::start_open(http_client, std::move(request),
				static_cast<basic_open_diagnostics<Exec>*>(nullptr),
				stream_options, nullopt, timeout, std::forward<Handler>(handler)
			);
		},
		timeout, stream_options,
		unbound_redirect_time(std::forward<Token>(token)));
	}
}

template <core_concepts::exec Exec, http::version_enum Version, typename Token>
auto open(http::basic_client<Exec,Version> &http_client, connect_request request,
	basic_open_diagnostics<Exec> &diagnostics, Token &&token)
	requires (Version == http::version::v11) and
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>
{
	diagnostics.endpoint = request.endpoint;
	diagnostics.reply.reset();

	const auto stream_options = request.stream_options.value_or(stream_config{});
	const auto timeout = request.handshake_timeout.value_or(detail::default_handshake_timeout);

	if constexpr( is_error_code_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		auto adapted_error = adapt_error_code(token);

		detail::open_sync(http_client, std::move(request), &diagnostics,
			stream_options, nullopt, timeout, result, adapted_error.get()
		);
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		error_code error;

		detail::open_sync(http_client, std::move(request), &diagnostics,
			stream_options, nullopt, timeout, result, error
		);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::open");
		return result;
	}
	else
	{
		return detail::initiate_handshake_io<basic_stream<Exec>>(http_client.get_executor(),
		[&http_client, request = std::move(request), &diagnostics, stream_options, timeout]
		<typename Handler>(Handler &&handler) mutable
		{
			detail::start_open(http_client, std::move(request), &diagnostics,
				stream_options, nullopt, timeout, std::forward<Handler>(handler)
			);
		},
		timeout, stream_options,
		unbound_redirect_time(std::forward<Token>(token)));
	}
}

template <core_concepts::exec Exec, http::version_enum Version, typename Token>
auto open(http::basic_client<Exec,Version> &http_client, url endpoint, Token &&token)
	requires (Version == http::version::v11) and
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>
{
	return open(http_client, connect_request(std::move(endpoint)),
		std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec, http::version_enum Version, typename Token>
auto open(http::basic_client<Exec,Version> &http_client, url endpoint,
	basic_open_diagnostics<Exec> &diagnostics, Token &&token)
	requires (Version == http::version::v11) and
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_stream<Exec>>
{
	return open(http_client, connect_request(std::move(endpoint)),
		diagnostics, std::forward<Token>(token)
	);
}

} //namespace riwo::websocket

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_WEBSOCKET_DETAIL_CLIENT_H
