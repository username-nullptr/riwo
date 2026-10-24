// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H
#define RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H

#include <riwo/websocket_exp/protocol/handshake.h>
#include <riwo/websocket_exp/detail/handshake_io.h>
#include <riwo/websocket_exp/detail/permessage_deflate.h>
#include <riwo/core/algorithm/secure_random.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::websocket_exp::detail
{

inline bool equal_ci(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

inline bool websocket_owned_request_header(std::string_view name) noexcept
{
	return equal_ci(name, "Host") or equal_ci(name, "Connection") or
		equal_ci(name, "Upgrade") or equal_ci(name, "Proxy-Authorization") or
		(name.size() >= 14 and equal_ci(name.substr(0, 14), "Sec-WebSocket-"));
}

inline sys_expected<url> canonical_websocket_url(const url &endpoint) noexcept
{
	try {
		if( not endpoint.is_valid() or endpoint.host().empty() or
			endpoint.has_fragment() )
			return sys_unexpected(make_system_error_code(
				std::errc::invalid_argument));
		auto scheme = strtls::to_lower(endpoint.protocol());
		if( scheme != "ws" and scheme != "wss" and
			scheme != "http" and scheme != "https" )
			return sys_unexpected(make_system_error_code(
				std::errc::protocol_not_supported));
		auto text = endpoint.to_string();
		auto separator = text.find("://");
		if( separator == std::string::npos )
			return sys_unexpected(make_system_error_code(
				std::errc::invalid_argument));
		text.replace(0, separator,
			scheme == "wss" or scheme == "https" ? "wss" : "ws");
		return url(std::move(text));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

inline sys_expected<url> websocket_transport_url(const url &endpoint) noexcept
{
	try {
		auto canonical = canonical_websocket_url(endpoint);
		if( not canonical ) return sys_unexpected(canonical.error());
		auto text = canonical->to_string();
		auto separator = text.find("://");
		text.replace(0, separator,
			equal_ci(canonical->protocol(), "wss") ? "https" : "http");
		return url(std::move(text));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

inline error_code validate_open_request(connect_request &request,
	const stream_config &stream) noexcept
{
	try {
		auto endpoint = canonical_websocket_url(request.endpoint);
		if( not endpoint ) return endpoint.error();
		request.endpoint = std::move(*endpoint);
		if( stream.read_buffer_size == 0 or
			stream.ping_interval < std::chrono::milliseconds::zero() or
			stream.compression.level < -1 or stream.compression.level > 9 )
			return make_system_error_code(std::errc::invalid_argument);
		if( not supported_extension_offers(request.extensions) )
			return make_error_code(errc::unsupported_extension);
		for( const auto &field : request.headers )
			if( websocket_owned_request_header(field.name) )
				return make_error_code(errc::invalid_upgrade);
		return {};
	}
	catch(...) {
		return exception_error(std::current_exception());
	}
}

inline uint16_t websocket_effective_port(const url &value) noexcept
{
	if( value.port() != 0 ) return value.port();
	return equal_ci(value.protocol(), "wss") ? 443 : 80;
}

inline bool same_websocket_origin(const url &lhs, const url &rhs) noexcept
{
	return equal_ci(lhs.protocol(), rhs.protocol()) and
		equal_ci(lhs.host(), rhs.host()) and
		websocket_effective_port(lhs) == websocket_effective_port(rhs);
}

inline bool websocket_redirect_status(http_exp::status status) noexcept
{
	return status == http_exp::status::moved_permanently or
		status == http_exp::status::found or
		status == http_exp::status::see_other or
		status == http_exp::status::temporary_redirect or
		status == http_exp::status::permanent_redirect;
}

inline bool extended_connect(const http_exp::client_config &config) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	return config.protocols.requirement ==
		http_exp::protocol_requirement::require_http2;
#else
	ignore_unused(config);
	return false;
#endif
}

inline http_exp::client_config websocket_http_config(
	http_exp::client_config config, std::chrono::milliseconds handshake_timeout)
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( config.protocols.requirement ==
		http_exp::protocol_requirement::require_http2 )
		config.h2.enable_extended_connect = true;
	else
#endif
		config.protocols.requirement =
			http_exp::protocol_requirement::require_http1;
	config.redirects.max_redirects = 0;
	config.automatic_decompression = false;
	if( handshake_timeout > std::chrono::milliseconds::zero() )
	{
		config.timeout.connect = std::min(config.timeout.connect,
			handshake_timeout);
		config.timeout.header = std::min(config.timeout.header,
			handshake_timeout);
		config.timeout.pool_wait = std::min(config.timeout.pool_wait,
			handshake_timeout);
	}
	return config;
}

struct prepared_open
{
	prepared_open(http_exp::request request_value, http_exp::body_spec spec_value,
		opening_request opening_value) :
		request(std::move(request_value)), spec(std::move(spec_value)),
		opening(std::move(opening_value)) {}

	http_exp::request request;
	http_exp::body_spec spec;
	opening_request opening;
};

inline sys_expected<prepared_open> prepare_open(const connect_request &request,
	const url &endpoint, bool use_extended_connect) noexcept
{
	try {
		auto target = websocket_transport_url(endpoint);
		if( not target ) return sys_unexpected(target.error());

		std::array<std::byte,16> nonce {};
		if( auto random = ::riwo::secure_random_bytes(asio::buffer(nonce)); not random )
			return sys_unexpected(random.error());
		auto key = make_client_key(nonce);
		if( not key ) return sys_unexpected(key.error());

		opening_request opening {
			.key = std::move(*key),
			.subprotocols = request.subprotocols,
			.extensions = request.extensions,
		};
		auto version = http_exp::protocol::v1_1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( use_extended_connect ) version = http_exp::protocol::v2;
#endif
		auto handshake = make_opening_request_headers(opening, version);
		if( not handshake ) return sys_unexpected(handshake.error());

		http_exp::request result(
			use_extended_connect ? http_exp::method::connect :
				http_exp::method::get, std::move(*target));
		for( const auto &field : request.headers )
			result.headers().add(field.name, field.value);
		for( const auto &field : *handshake )
		{
			if( not use_extended_connect and
				(equal_ci(field.name, "Connection") or
				 equal_ci(field.name, "Upgrade")) )
				continue;
			result.headers().set(field.name, field.value);
		}
		if( use_extended_connect ) result.connect_protocol("websocket");
		else result.upgrade_protocol("websocket");

		auto spec = use_extended_connect ? http_exp::body_spec::streaming() :
			http_exp::body_spec::empty();
		return prepared_open(std::move(result), std::move(spec),
			std::move(opening));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

enum class open_response_action : uint8_t { accept, redirect, reject };

struct open_response_decision
{
	open_response_action action = open_response_action::reject;
	error_code error {};
	url redirect_endpoint {};
	opening_response response {};
	bool clear_credentials = false;
};

inline open_response_decision evaluate_open_response(
	const http_exp::response_head &response, const opening_request &opening,
	const connect_request &request, const url &endpoint,
	http_exp::protocol selected_protocol, size_t redirects) noexcept
{
	open_response_decision result;
	try {
		if( websocket_redirect_status(response.status) )
		{
			if( redirects >= request.max_redirects )
			{
				result.error = make_error_code(errc::redirect_limit_exceeded);
				return result;
			}
			auto location = response.location();
			if( not location )
			{
				result.error = make_error_code(errc::handshake_rejected);
				return result;
			}
			auto probe = request;
			probe.endpoint = url::resolve(endpoint, *location);
			const auto stream = request.stream_options.value_or(stream_config{});
			if( auto error = validate_open_request(probe, stream) )
			{
				result.error = error;
				return result;
			}
			if( equal_ci(endpoint.protocol(), "wss") and
				equal_ci(probe.endpoint.protocol(), "ws") and
				not request.allow_insecure_redirects )
			{
				result.error = make_error_code(errc::insecure_redirect);
				return result;
			}
			result.action = open_response_action::redirect;
			result.clear_credentials =
				not same_websocket_origin(endpoint, probe.endpoint);
			result.redirect_endpoint = std::move(probe.endpoint);
			return result;
		}
		auto accepted = parse_opening_response(response.status, response.headers,
			opening, selected_protocol);
		if( not accepted )
		{
			result.error = accepted.error();
			return result;
		}
		if( not supported_extension_response(
			accepted->extensions, request.extensions) )
		{
			result.error = make_error_code(errc::unsupported_extension);
			return result;
		}
		result.action = open_response_action::accept;
		result.response = std::move(*accepted);
		return result;
	}
	catch(...) {
		result.error = exception_error(std::current_exception());
	}
	return result;
}

inline std::chrono::milliseconds remaining_timeout(
	std::chrono::steady_clock::time_point deadline) noexcept
{
	const auto now = std::chrono::steady_clock::now();
	if( now >= deadline ) return std::chrono::milliseconds::zero();
	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
		deadline - now);
	return remaining > std::chrono::milliseconds::zero() ? remaining :
		std::chrono::milliseconds(1);
}

template <core_concepts::exec Exec>
class open_cancellation_state
{
	using exchange_t = http_exp::basic_client_exchange<Exec>;

public:
	void activate(const std::shared_ptr<exchange_t> &exchange) noexcept
	{
		bool cancel_now = false;
		{
			std::lock_guard lock(m_mutex);
			m_active = exchange;
			cancel_now = m_cancelled;
		}
		if( cancel_now ) exchange->cancel();
	}

	void clear(const std::shared_ptr<exchange_t> &exchange) noexcept
	{
		std::lock_guard lock(m_mutex);
		if( m_active.lock() == exchange ) m_active.reset();
	}

	void cancel() noexcept
	{
		std::shared_ptr<exchange_t> active;
		{
			std::lock_guard lock(m_mutex);
			m_cancelled = true;
			active = m_active.lock();
		}
		if( active ) active->cancel();
	}

	[[nodiscard]] bool cancelled() const noexcept
	{
		std::lock_guard lock(m_mutex);
		return m_cancelled;
	}

private:
	mutable std::mutex m_mutex;
	std::weak_ptr<exchange_t> m_active;
	bool m_cancelled = false;
};

inline error_code cancellation_error(bool cancelled,
	error_code error = {}) noexcept
{
	return cancelled ? asio::error::make_error_code(
		asio::error::operation_aborted) : error;
}

template <core_concepts::exec Exec>
void open_sync(http_exp::basic_client<Exec> &http_client,
	connect_request request, basic_open_diagnostics<Exec> *diagnostics,
	stream_config stream_options, std::chrono::milliseconds timeout,
	bool use_extended_connect, basic_stream<Exec> &result,
	error_code &error) noexcept
{
	try {
		if( diagnostics )
		{
			diagnostics->endpoint = request.endpoint;
			diagnostics->response.reset();
			diagnostics->selected_protocol.reset();
		}
		error = validate_open_request(request, stream_options);
		if( error ) return;
		if( diagnostics ) diagnostics->endpoint = request.endpoint;
		if( timeout <= std::chrono::milliseconds::zero() )
		{
			error = asio::error::make_error_code(asio::error::timed_out);
			return;
		}
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		auto endpoint = request.endpoint;
		size_t redirects = 0;
		for(;;)
		{
			if( remaining_timeout(deadline) <=
				std::chrono::milliseconds::zero() )
			{
				error = asio::error::make_error_code(asio::error::timed_out);
				return;
			}
			auto prepared = prepare_open(request, endpoint,
				use_extended_connect);
			if( not prepared ) { error = prepared.error(); return; }
			auto exchange = http_client.open(std::move(prepared->request),
				std::move(prepared->spec), error);
			if( error ) return;
			auto reply_head_value = exchange.wait_reply(error);
			if( error ) return;
			const auto selected_protocol = exchange.selected_protocol();
			auto decision = evaluate_open_response(reply_head_value,
				prepared->opening, request, endpoint, selected_protocol,
				redirects);
			if( decision.action == open_response_action::redirect )
			{
				exchange.cancel();
				if( decision.clear_credentials )
				{
					request.headers.erase("Authorization");
					request.headers.erase("Cookie");
				}
				endpoint = std::move(decision.redirect_endpoint);
				++redirects;
				continue;
			}
			if( diagnostics )
			{
				diagnostics->endpoint = endpoint;
				diagnostics->response = reply_head_value;
				diagnostics->selected_protocol = selected_protocol;
			}
			if( decision.action == open_response_action::reject )
			{
				error = decision.error;
				exchange.cancel();
				return;
			}
			auto channel = exchange.release_duplex(error);
			if( error ) return;
			result.adopt(
				std::make_shared<http_exp::basic_duplex_channel<Exec>>(
					std::move(channel)), adopt_options {
						.stream_role = role::client,
						.negotiated_subprotocol =
							decision.response.subprotocol.value_or(""),
						.negotiated_extensions =
							std::move(decision.response.extensions),
					}, error);
			return;
		}
	}
	catch(...) { error = exception_error(std::current_exception()); }
}

template <core_concepts::exec Exec>
asio::awaitable<optional<std::tuple<error_code,basic_stream<Exec>>>,Exec>
co_open(http_exp::basic_client<Exec> *http_client, connect_request request,
	basic_open_diagnostics<Exec> *diagnostics, stream_config stream_options,
	std::chrono::milliseconds timeout, bool use_extended_connect,
	std::shared_ptr<open_cancellation_state<Exec>> cancellation)
{
	basic_stream<Exec> result(http_client->get_executor(), stream_options);
	try {
		if( cancellation->cancelled() )
			co_return std::tuple {
				asio::error::make_error_code(asio::error::operation_aborted),
				std::move(result)};
		if( diagnostics )
		{
			diagnostics->endpoint = request.endpoint;
			diagnostics->response.reset();
			diagnostics->selected_protocol.reset();
		}
		if( auto error = validate_open_request(request, stream_options) )
			co_return std::tuple {error, std::move(result)};
		if( diagnostics ) diagnostics->endpoint = request.endpoint;
		if( timeout <= std::chrono::milliseconds::zero() )
			co_return std::tuple {
				asio::error::make_error_code(asio::error::timed_out),
				std::move(result)};
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		auto endpoint = request.endpoint;
		size_t redirects = 0;
		for(;;)
		{
			if( cancellation->cancelled() ) co_return std::tuple {
				asio::error::make_error_code(asio::error::operation_aborted),
				std::move(result)};
			if( remaining_timeout(deadline) <=
				std::chrono::milliseconds::zero() )
				co_return std::tuple {
					asio::error::make_error_code(asio::error::timed_out),
					std::move(result)};
			auto prepared = prepare_open(request, endpoint,
				use_extended_connect);
			if( not prepared ) co_return std::tuple {
				prepared.error(), std::move(result)};
			auto [open_error, exchange_value] = co_await http_client->open(
				std::move(prepared->request), std::move(prepared->spec),
				asio::as_tuple(asio::use_awaitable_t<Exec>{}));
			if( open_error ) co_return std::tuple {
				cancellation_error(cancellation->cancelled(), open_error),
				std::move(result)};
			auto exchange = std::make_shared<
				http_exp::basic_client_exchange<Exec>>(
					std::move(exchange_value));
			cancellation->activate(exchange);
			auto [head_error, reply_head_value] = co_await exchange->wait_reply(
				asio::as_tuple(asio::use_awaitable_t<Exec>{}));
			cancellation->clear(exchange);
			if( head_error ) co_return std::tuple {
				cancellation_error(cancellation->cancelled(), head_error),
				std::move(result)};
			if( cancellation->cancelled() ) co_return std::tuple {
				asio::error::make_error_code(asio::error::operation_aborted),
				std::move(result)};
			const auto selected_protocol = exchange->selected_protocol();
			auto decision = evaluate_open_response(reply_head_value,
				prepared->opening, request, endpoint, selected_protocol,
				redirects);
			if( decision.action == open_response_action::redirect )
			{
				exchange->cancel();
				if( decision.clear_credentials )
				{
					request.headers.erase("Authorization");
					request.headers.erase("Cookie");
				}
				endpoint = std::move(decision.redirect_endpoint);
				++redirects;
				continue;
			}
			if( diagnostics )
			{
				diagnostics->endpoint = endpoint;
				diagnostics->response = reply_head_value;
				diagnostics->selected_protocol = selected_protocol;
			}
			if( decision.action == open_response_action::reject )
			{
				exchange->cancel();
				co_return std::tuple {
					decision.error, std::move(result)};
			}
			cancellation->activate(exchange);
			auto [channel_error, channel] = co_await exchange->release_duplex(
				asio::as_tuple(asio::use_awaitable_t<Exec>{}));
			cancellation->clear(exchange);
			if( channel_error ) co_return std::tuple {
				cancellation_error(cancellation->cancelled(), channel_error),
				std::move(result)};
			if( cancellation->cancelled() ) co_return std::tuple {
				asio::error::make_error_code(asio::error::operation_aborted),
				std::move(result)};
			error_code adopt_error;
			result.adopt(
				std::make_shared<http_exp::basic_duplex_channel<Exec>>(
					std::move(channel)), adopt_options {
						.stream_role = role::client,
						.negotiated_subprotocol =
							decision.response.subprotocol.value_or(""),
						.negotiated_extensions =
							std::move(decision.response.extensions),
					}, adopt_error);
			co_return std::tuple {adopt_error, std::move(result)};
		}
	}
	catch(...) {
		co_return std::tuple {exception_error(std::current_exception()),
			std::move(result)};
	}
}

template <core_concepts::exec Exec, typename Handler>
void start_open(http_exp::basic_client<Exec> &http_client,
	connect_request request, basic_open_diagnostics<Exec> *diagnostics,
	stream_config stream_options, std::chrono::milliseconds timeout,
	bool use_extended_connect, Handler &&handler)
{
	auto executor = http_client.get_executor();
	auto cancellation = std::make_shared<open_cancellation_state<Exec>>();
	auto coroutine_cancellation =
		std::make_shared<asio::cancellation_signal>();
	auto slot = asio::get_associated_cancellation_slot(handler);
	if( slot.is_connected() )
	{
		std::weak_ptr<open_cancellation_state<Exec>> weak = cancellation;
		std::weak_ptr<asio::cancellation_signal> weak_signal =
			coroutine_cancellation;
		slot.assign([weak, weak_signal](
			asio::cancellation_type type) noexcept {
			if( type != asio::cancellation_type::none )
			{
				if( auto active = weak.lock() ) active->cancel();
				if( auto signal = weak_signal.lock() ) signal->emit(type);
			}
		});
	}
	auto completion =
	[handler = std::forward<Handler>(handler), executor, slot,
	 cancellation, coroutine_cancellation]
	(std::exception_ptr exception,
	 optional<std::tuple<error_code,basic_stream<Exec>>> value) mutable
	{
		ignore_unused(cancellation, coroutine_cancellation);
		if( slot.is_connected() ) slot.clear();
		if( exception )
			std::move(handler)(exception_error(exception),
				basic_stream<Exec>(executor));
		else if( value )
			std::move(handler)(std::get<0>(*value),
				std::move(std::get<1>(*value)));
		else
			std::move(handler)(asio::error::operation_aborted,
				basic_stream<Exec>(executor));
	};
	asio::co_spawn(executor, co_open(&http_client, std::move(request),
		diagnostics, std::move(stream_options), timeout,
		use_extended_connect, cancellation),
		asio::bind_cancellation_slot(coroutine_cancellation->slot(),
			std::move(completion)));
}

} //namespace riwo::websocket_exp::detail

namespace riwo::websocket_exp
{

template <core_concepts::exec Exec>
class basic_client<Exec>::impl : public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	struct open_operation : std::enable_shared_from_this<open_operation>
	{
		using complete_fn_t = void (*)(open_operation&, error_code, stream_t);
		open_operation(std::shared_ptr<impl> owner_value,
			complete_fn_t completion) : owner(std::move(owner_value)),
			complete_fn(completion) {}
		void complete(error_code error, stream_t stream) {
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
		using associated_executor_t =
			asio::associated_executor_t<Handler,executor_t>;
		using allocator_t = asio::associated_allocator_t<Handler>;
		using cancellation_slot_t =
			asio::associated_cancellation_slot_t<Handler>;

		open_handler_operation(std::shared_ptr<impl> owner, Handler value,
			const executor_t &executor) :
			open_operation(std::move(owner), &self_t::complete_handler),
			handler(std::move(value)),
			executor(asio::get_associated_executor(handler, executor)),
			allocator(asio::get_associated_allocator(handler)),
			slot(asio::get_associated_cancellation_slot(handler)) {}

		void arm_cancellation(const std::shared_ptr<open_operation> &operation)
		{
			if( not slot.is_connected() ) return;
			std::weak_ptr<open_operation> weak = operation;
			slot.assign([weak](asio::cancellation_type type) noexcept {
				if( type != asio::cancellation_type::none )
					if( auto active = weak.lock() )
						active->cancellation.emit(type);
			});
		}

		static void complete_handler(open_operation &base, error_code error,
			stream_t stream)
		{
			auto &self = static_cast<self_t&>(base);
			if( self.slot.is_connected() ) self.slot.clear();
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
		associated_executor_t executor;
		allocator_t allocator;
		cancellation_slot_t slot;
	};

	struct open_completion_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<open_operation> operation;
		[[nodiscard]] cancellation_slot_type
		get_cancellation_slot() const noexcept {
			return operation->cancellation.slot();
		}
		void operator()(error_code error, stream_t stream) {
			operation->complete(error, std::move(stream));
		}
	};

public:
	impl(executor_type executor_value, config_t config_value) :
		m_exec(std::move(executor_value)),
		m_config(validate_config(std::move(config_value))),
		m_use_extended_connect(detail::extended_connect(m_config.http)),
		m_http(m_exec, detail::websocket_http_config(m_config.http,
			m_config.handshake_timeout)) {}

	impl(http_client_t &&http_client, config_t config_value) :
		m_exec(http_client.get_executor()),
		m_config(validate_config(std::move(config_value))),
		m_use_extended_connect(detail::extended_connect(http_client.config())),
		m_http(std::move(http_client)) {}

	[[nodiscard]] static config_t validate_config(config_t config)
	{
		if( config.stream.read_buffer_size == 0 or
			config.stream.ping_interval < std::chrono::milliseconds::zero() or
			config.stream.compression.level < -1 or
			config.stream.compression.level > 9 )
			throw std::invalid_argument(
				"riwo::websocket_exp::basic_client: invalid stream config");
		return config;
	}

	void prepare_request(connect_request_t &request) const
	{
		if( not request.stream_options )
			request.stream_options = m_config.stream;
		if( not request.handshake_timeout )
			request.handshake_timeout = m_config.handshake_timeout;
	}

	void retain_operation(const std::shared_ptr<open_operation> &operation)
	{
		std::lock_guard lock(m_mutex);
		std::erase_if(m_operations,
			[](const auto &item) { return item.expired(); });
		m_operations.emplace_back(operation);
		m_pending_open_count.fetch_add(1, std::memory_order_relaxed);
	}

	void release_operation(
		const std::shared_ptr<open_operation> &operation) noexcept
	{
		if( operation->finished.exchange(true, std::memory_order_acq_rel) )
			return;
		m_pending_open_count.fetch_sub(1, std::memory_order_relaxed);
		std::lock_guard lock(m_mutex);
		std::erase_if(m_operations, [&](const auto &item) {
			auto value = item.lock();
			return not value or value == operation;
		});
	}

	template <typename Token>
	[[nodiscard]] auto async_open(connect_request_t request,
		diagnostics_t *diagnostics, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		prepare_request(request);
		auto self = this->shared_from_this();
		return asio::async_initiate<token_t,void(error_code,stream_t)>(
		[self = std::move(self), request = std::move(request), diagnostics]
		(auto completion_handler) mutable
		{
			using handler_t = decltype(completion_handler);
			using operation_t = open_handler_operation<handler_t>;
			auto operation = std::make_shared<operation_t>(self,
				std::move(completion_handler), self->m_exec);
			self->retain_operation(operation);
			operation->arm_cancellation(operation);
			const auto stream_options = *request.stream_options;
			const auto timeout = *request.handshake_timeout;
			detail::start_handshake_io<stream_t>(self->m_exec,
			[client = &self->m_http, request = std::move(request), diagnostics,
			 stream_options, timeout,
			 extended = self->m_use_extended_connect]
			(auto handler) mutable {
				detail::start_open(*client, std::move(request), diagnostics,
					stream_options, timeout, extended, std::move(handler));
			}, detail::effective_handshake_timeout(timeout), stream_options,
			open_completion_handler{std::move(operation)});
		}, completion_token);
	}

	[[nodiscard]] stream_t open_sync(connect_request_t request,
		diagnostics_t *diagnostics, error_code &error) noexcept
	{
		try {
			prepare_request(request);
			m_pending_open_count.fetch_add(1, std::memory_order_relaxed);
			struct guard_t {
				std::atomic_size_t &value;
				~guard_t() { value.fetch_sub(1, std::memory_order_relaxed); }
			} guard {m_pending_open_count};
			const auto stream_options = *request.stream_options;
			const auto timeout = *request.handshake_timeout;
			stream_t result(m_exec, stream_options);
			detail::open_sync(m_http, std::move(request), diagnostics,
				stream_options, timeout, m_use_extended_connect, result, error);
			return result;
		}
		catch(...) { error = exception_error(std::current_exception()); }
		return stream_t(m_exec, m_config.stream);
	}

	void cancel() noexcept
	{
		std::vector<std::shared_ptr<open_operation>> operations;
		{
			std::lock_guard lock(m_mutex);
			for( auto &item : m_operations )
				if( auto operation = item.lock() )
					operations.emplace_back(std::move(operation));
		}
		for( auto &operation : operations )
			operation->cancellation.emit(asio::cancellation_type::all);
	}

	executor_type m_exec;
	config_t m_config;
	bool m_use_extended_connect = false;
	http_client_t m_http;
	std::atomic_size_t m_pending_open_count {0};
	std::mutex m_mutex;
	std::vector<std::weak_ptr<open_operation>> m_operations;
};

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client() requires
	core_concepts::match_sched<io_executor_t,executor_type> :
	basic_client(executor_type(riwo::get_executor())) {}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(config_t config) requires
	core_concepts::match_sched<io_executor_t,executor_type> :
	basic_client(executor_type(riwo::get_executor()), std::move(config)) {}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor, config_t config) requires (
	not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
	core_concepts::match_sched<Exec0,executor_type>) :
	m_impl(std::make_shared<impl>(get_executor_helper(
		std::forward<Exec0>(executor)), std::move(config))) {}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(http_client_t &&http_client,
	config_t config) :
	m_impl(std::make_shared<impl>(std::move(http_client),
		std::move(config))) {}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(basic_client &&other) noexcept = default;

template <core_concepts::exec Exec>
basic_client<Exec> &basic_client<Exec>::operator=(
	basic_client &&other) noexcept = default;

template <core_concepts::exec Exec>
basic_client<Exec>::~basic_client() = default;

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open_impl(connect_request_t request,
	diagnostics_t *diagnostics, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->open_sync(std::move(request), diagnostics,
			adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->open_sync(std::move(request), diagnostics, error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::basic_client::open");
		return result;
	}
	else
		return m_impl->async_open(std::move(request), diagnostics,
			std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request, Token &&token)
	requires open_token_v<Token> {
	return open_impl(std::move(request), nullptr, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(connect_request_t request,
	diagnostics_t &diagnostics, Token &&token) requires open_token_v<Token> {
	return open_impl(std::move(request), &diagnostics,
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, Token &&token)
	requires open_token_v<Token> {
	return open(connect_request_t(std::move(endpoint)),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(url endpoint, diagnostics_t &diagnostics,
	Token &&token) requires open_token_v<Token> {
	return open(connect_request_t(std::move(endpoint)), diagnostics,
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
std::shared_ptr<http_exp::cookie_jar>
basic_client<Exec>::cookie_store() const noexcept {
	return m_impl->m_http.cookie_store();
}

template <core_concepts::exec Exec>
size_t basic_client<Exec>::pending_open_count() const noexcept {
	return m_impl->m_pending_open_count.load(std::memory_order_relaxed);
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::config() const -> config_t {
	return m_impl->m_config;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() const noexcept -> const http_client_t& {
	return m_impl->m_http;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::http_client() noexcept -> http_client_t& {
	return m_impl->m_http;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
basic_client<Exec> &basic_client<Exec>::cancel() noexcept {
	m_impl->cancel();
	return *this;
}

namespace detail
{

template <core_concepts::exec Exec, typename Token>
auto external_open(http_exp::basic_client<Exec> &http_client,
	connect_request request, basic_open_diagnostics<Exec> *diagnostics,
	Token &&token)
{
	const auto stream_options = request.stream_options.value_or(stream_config{});
	const auto timeout = request.handshake_timeout.value_or(
		std::chrono::milliseconds(30000));
	const auto use_extended_connect = extended_connect(http_client.config());
	if constexpr( is_error_code_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		auto adapted_error = adapt_error_code(token);
		open_sync(http_client, std::move(request), diagnostics, stream_options,
			timeout, use_extended_connect, result, adapted_error.get());
		return result;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		basic_stream<Exec> result(http_client.get_executor(), stream_options);
		error_code error;
		open_sync(http_client, std::move(request), diagnostics, stream_options,
			timeout, use_extended_connect, result, error);
		if( error ) system_error::loc_throw(error,
			"riwo::websocket_exp::open");
		return result;
	}
	else
	{
		return initiate_handshake_io<basic_stream<Exec>>(
			http_client.get_executor(),
		[&http_client, request = std::move(request), diagnostics,
		 stream_options, timeout, use_extended_connect]
		(auto handler) mutable {
			start_open(http_client, std::move(request), diagnostics,
				stream_options, timeout, use_extended_connect,
				std::move(handler));
		}, timeout, stream_options, std::forward<Token>(token));
	}
}

} //namespace detail

template <core_concepts::exec Exec, typename Token>
auto open(http_exp::basic_client<Exec> &http_client,
	connect_request request, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_stream<Exec>> {
	return detail::external_open(http_client, std::move(request),
		static_cast<basic_open_diagnostics<Exec>*>(nullptr),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto open(http_exp::basic_client<Exec> &http_client,
	connect_request request, basic_open_diagnostics<Exec> &diagnostics,
	Token &&token) requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_stream<Exec>> {
	return detail::external_open(http_client, std::move(request),
		&diagnostics, std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto open(http_exp::basic_client<Exec> &http_client, url endpoint,
	Token &&token) requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_stream<Exec>> {
	return open(http_client, connect_request(std::move(endpoint)),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec, typename Token>
auto open(http_exp::basic_client<Exec> &http_client, url endpoint,
	basic_open_diagnostics<Exec> &diagnostics, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_stream<Exec>> {
	return open(http_client, connect_request(std::move(endpoint)), diagnostics,
		std::forward<Token>(token));
}

} //namespace riwo::websocket_exp

#endif //RIWO_WEBSOCKET_EXP_DETAIL_CLIENT_H
