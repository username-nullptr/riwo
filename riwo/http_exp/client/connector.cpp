// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/connector.h"
#include "detail/factory.h"
#include <riwo/http_exp/protocol/detail/h1/session.h>
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
# include <riwo/http_exp/protocol/detail/h2/session.h>
#endif
#include <riwo/http_exp/transport/tcp_connection.h>

#if RIWO_OPENSSL_SUPPORT
# include <riwo/http_exp/transport/tls_connection.h>
# include <openssl/ssl.h>
#endif

#include <atomic>
#include <array>
#include <asio/as_tuple.hpp>
#include <asio/connect.hpp>
#include <asio/co_spawn.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <thread>
#include <vector>

#if !defined(_WIN32)
# include <poll.h>
# include <sys/socket.h>
#endif

namespace riwo::http_exp::detail { namespace
{

constexpr bool h2_client_session_available =
	RIWO_HTTP_EXP_HTTP2_SUPPORT != 0;

[[nodiscard]] std::vector<asio::ip::tcp::endpoint> resolve_with_timeout(
	asio::any_io_executor executor, std::string_view host, uint16_t port,
	std::chrono::milliseconds timeout, error_code &error)
{
	error.clear();
	if( timeout.count() <= 0 )
	{
		asio::ip::tcp::resolver resolver(std::move(executor));
		auto results = resolver.resolve(host, std::to_string(port), error);
		std::vector<asio::ip::tcp::endpoint> endpoints;
		if( error ) return endpoints;
		for( const auto &value : results )
			endpoints.push_back(value.endpoint());
		return endpoints;
	}

	asio::io_context context;
	asio::ip::tcp::resolver resolver(context);
	std::vector<asio::ip::tcp::endpoint> endpoints;
	bool completed = false;
	resolver.async_resolve(host, std::to_string(port),
	[&](error_code resolve_error,
		asio::ip::tcp::resolver::results_type results)
	{
		error = resolve_error;
		if( not error )
			for( const auto &value : results )
				endpoints.push_back(value.endpoint());
		completed = true;
	});
	context.run_for(timeout);
	if( not completed )
	{
		error = asio::error::make_error_code(asio::error::timed_out);
		resolver.cancel();
	}
	return endpoints;
}

template <typename Socket>
void connect_with_timeout(Socket &socket,
	const std::vector<asio::ip::tcp::endpoint> &endpoints,
	std::chrono::milliseconds timeout, error_code &error)
{
	error.clear();
	if( timeout.count() <= 0 )
	{
		asio::connect(socket, endpoints, error);
		return;
	}

#if !defined(_WIN32)
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	for( const auto &endpoint : endpoints )
	{
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		if( remaining.count() <= 0 )
		{
			error = asio::error::make_error_code(asio::error::timed_out);
			return;
		}
		socket.open(endpoint.protocol(), error);
		if( error ) return;
		socket.non_blocking(true, error);
		if( error ) return;
		const auto native_result = ::connect(socket.native_handle(),
			endpoint.data(), static_cast<socklen_t>(endpoint.size()));
		if( native_result != 0 and errno != EINPROGRESS and errno != EWOULDBLOCK )
		{
			error = std::error_code(errno, std::system_category());
			error_code ignored;
			socket.close(ignored);
			continue;
		}
		if( native_result != 0 )
		{
			pollfd descriptor {
				.fd = socket.native_handle(),
				.events = POLLOUT,
				.revents = 0
			};
			const auto result = ::poll(&descriptor, 1,
				static_cast<int>(remaining.count()));
			if( result == 0 )
			{
				error = asio::error::make_error_code(asio::error::timed_out);
				error_code ignored;
				socket.close(ignored);
				return;
			}
			if( result < 0 )
			{
				error = asio::error::make_error_code(
					asio::error::try_again);
				error_code ignored;
				socket.close(ignored);
				return;
			}
			int value = 0;
			socklen_t size = sizeof(value);
			if( ::getsockopt(socket.native_handle(), SOL_SOCKET, SO_ERROR,
				&value, &size) != 0 or value != 0 )
			{
				error = std::error_code(value != 0 ? value : errno,
					std::system_category());
				error_code ignored;
				socket.close(ignored);
				continue;
			}
		}
		socket.non_blocking(false, error);
		if( error )
		{
			error_code ignored;
			socket.close(ignored);
			return;
		}
		return;
	}
	if( not error )
		error = asio::error::make_error_code(asio::error::host_unreachable);
#else
	std::atomic<bool> finished {false};
	std::atomic<bool> timed_out {false};
	std::jthread watchdog([&]
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while( not finished and std::chrono::steady_clock::now() < deadline )
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		if( finished ) return;
		timed_out = true;
		error_code ignored;
		socket.close(ignored);
	});
	asio::connect(socket, endpoints, error);
	finished = true;
	watchdog.request_stop();
	if( timed_out )
		error = asio::error::make_error_code(asio::error::timed_out);
#endif
}

#if RIWO_OPENSSL_SUPPORT
void tls_handshake_with_timeout(tls_socket &socket,
	std::chrono::milliseconds timeout, error_code &error)
{
	error.clear();
	if( timeout.count() <= 0 )
	{
		socket.handshake(asio::ssl::stream_base::client, error);
		return;
	}
	std::atomic<bool> finished {false};
	std::atomic<bool> timed_out {false};
	std::jthread watchdog([&]
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while( not finished and std::chrono::steady_clock::now() < deadline )
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		if( finished ) return;
		timed_out = true;
		error_code ignored;
		socket.next_layer().close(ignored);
	});
	socket.handshake(asio::ssl::stream_base::client, error);
	finished = true;
	watchdog.request_stop();
	if( timed_out )
		error = asio::error::make_error_code(asio::error::timed_out);
}
#endif

[[nodiscard]] std::string lower(std::string_view text)
{
	std::string result(text);
	std::ranges::transform(result, result.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return result;
}

struct proxy_target
{
	std::string host {};
	uint16_t port = 80;
};

[[nodiscard]] optional<proxy_target> parse_proxy(const session_key &key)
{
	if( not key.proxy.endpoint ) return std::nullopt;
	riwo::url value(*key.proxy.endpoint);
	if( not value.is_valid() or lower(value.protocol()) != "http" or
		value.host().empty() )
		return std::nullopt;
	return proxy_target {
		.host = std::string(value.host()),
		.port = static_cast<uint16_t>(value.port() == 0 ? 80 : value.port())
	};
}

[[maybe_unused,nodiscard]] std::string origin_authority(
	const session_key &key)
{
	auto host = key.host;
	if( host.find(':') != std::string::npos and not host.starts_with('[') )
		host = '[' + host + ']';
	return host + ':' + std::to_string(key.port);
}

[[nodiscard]] bool valid_policy(const protocol_policy_identity &policy) noexcept
{
	if( policy.preference.empty() ) return false;
	for( size_t index = 0; index < policy.preference.size(); ++index )
		for( size_t other = index + 1; other < policy.preference.size(); ++other )
			if( policy.preference[index] == policy.preference[other] )
				return false;
	return true;
}

[[nodiscard]] bool http1(protocol value) noexcept
{
	return value == protocol::v1_0 or value == protocol::v1_1;
}

[[nodiscard]] protocol_policy make_policy(const session_key &key)
{
	return protocol_policy {
		.preference = key.protocols.preference,
		.requirement = key.protocols.requirement
	};
}

void mix(size_t &seed, size_t value) noexcept
{
	seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
}

#if RIWO_OPENSSL_SUPPORT
[[nodiscard]] std::vector<unsigned char> alpn_wire(
	const std::vector<protocol> &preference)
{
	std::vector<unsigned char> result;
	for( const auto value : preference )
	{
		std::string_view name;
		if( value == protocol::v1_1 ) name = "http/1.1";
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
		else if( value == protocol::v2 ) name = "h2";
# endif
		else continue;
		result.push_back(static_cast<unsigned char>(name.size()));
		result.insert(result.end(), name.begin(), name.end());
	}
	return result;
}

[[nodiscard]] std::vector<protocol> alpn_preference(const session_key &key)
{
	std::vector<protocol> result;
	for( const auto value : key.protocols.preference )
	{
		if( value == protocol::v1_0 ) continue;
	#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( value == protocol::v2 and not h2_client_session_available ) continue;
	#endif
		if( key.protocols.requirement == protocol_requirement::require_http1 and
			value != protocol::v1_1 )
			continue;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( key.protocols.requirement == protocol_requirement::require_http2 and
			value != protocol::v2 )
			continue;
#endif
		result.push_back(value);
	}
	return result;
}

[[nodiscard]] optional<protocol> protocol_from_alpn(std::string_view value)
{
	if( value == "http/1.1" ) return protocol::v1_1;
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "h2" ) return protocol::v2;
# endif
	return {};
}

[[nodiscard]] error_code configure_tls_context(asio::ssl::context &context,
	const tls_identity &identity, error_code &error)
{
	context.set_options(asio::ssl::context::default_workarounds |
		asio::ssl::context::no_tlsv1 |
		asio::ssl::context::no_tlsv1_1 |
		asio::ssl::context::no_sslv2 |
		asio::ssl::context::no_sslv3, error);
	if( error ) return error;

	context.set_verify_mode(identity.verify_peer ?
		asio::ssl::verify_peer : asio::ssl::verify_none, error);
	if( error ) return error;
	if( not identity.verify_peer ) return {};

	if( identity.certificate_authority_file.empty() )
		context.set_default_verify_paths(error);
	else
		context.load_verify_file(identity.certificate_authority_file, error);
	return error;
}
#endif

template <typename Cancel>
void arm_async_timeout(asio::steady_timer &timer,
	std::chrono::milliseconds timeout, bool &expired, Cancel &&cancel)
{
	if( timeout.count() <= 0 )
		return;
	timer.expires_after(timeout);
	timer.async_wait(
	[&expired, cancel = std::forward<Cancel>(cancel)](error_code error) mutable
	{
		if( error ) return;
		expired = true;
		cancel();
	});
}

[[nodiscard]] asio::awaitable<sys_expected<negotiated_transport>>
connect_async_impl(asio::any_io_executor executor, session_key key,
	client_config config)
{
	try
	{
		auto selected = negotiate_protocol(make_policy(key), key);
		if( not selected )
			co_return sys_unexpected(selected.error());

		error_code error;
		auto proxy = parse_proxy(key);
		if( key.proxy.endpoint and not proxy )
			co_return sys_unexpected(make_error_code(errc::invalid_target));

		const auto connect_host = proxy ? proxy->host : key.host;
		const auto connect_port = proxy ? proxy->port : key.port;
		const auto connect_timeout = config.timeout.connect;
		error_code ignored;

		asio::ip::tcp::resolver resolver(executor);
		asio::steady_timer resolve_timer(executor);
		bool resolve_expired = false;
		arm_async_timeout(resolve_timer, connect_timeout, resolve_expired,
			[&resolver] { resolver.cancel(); });
		auto [resolve_error, results] = co_await resolver.async_resolve(
			connect_host, std::to_string(connect_port),
			asio::as_tuple(asio::use_awaitable));
		resolve_timer.cancel();
		if( resolve_expired )
			co_return sys_unexpected(asio::error::make_error_code(
				asio::error::timed_out));
		if( resolve_error )
			co_return sys_unexpected(resolve_error);
		if( results.empty() )
			co_return sys_unexpected(asio::error::make_error_code(
				asio::error::host_not_found));

		auto tcp = std::make_shared<tcp_connection>(executor);
		asio::steady_timer connect_timer(executor);
		bool connect_expired = false;
		arm_async_timeout(connect_timer, connect_timeout, connect_expired,
			[&tcp]
			{
				error_code ignored_error;
				tcp->socket().close(ignored_error);
			});
		auto [connect_error, endpoint] = co_await asio::async_connect(
			tcp->socket(), results,
			asio::as_tuple(asio::use_awaitable));
		(void)endpoint;
		connect_timer.cancel();
		if( connect_expired )
			co_return sys_unexpected(asio::error::make_error_code(
				asio::error::timed_out));
		if( connect_error )
			co_return sys_unexpected(connect_error);

		tcp->socket().set_option(asio::ip::tcp::no_delay(true), error);
		if( error ) co_return sys_unexpected(error);

		if( key.scheme != "https" )
		{
			auto remote = tcp->remote_endpoint(error);
			if( error ) co_return sys_unexpected(error);
			auto local = tcp->local_endpoint(error);
			if( error ) co_return sys_unexpected(error);
			co_return negotiated_transport {
				.stream = std::move(tcp),
				.selected_protocol = *selected,
				.remote = remote,
				.local = local,
				.absolute_form = proxy.has_value()
			};
		}

#if RIWO_OPENSSL_SUPPORT
		if( proxy )
		{
			const auto authority = origin_authority(key);
			const std::string request = "CONNECT " + authority +
				" HTTP/1.1\r\nHost: " + authority + "\r\n\r\n";
			asio::steady_timer proxy_timer(executor);
			bool proxy_expired = false;
			arm_async_timeout(proxy_timer, connect_timeout, proxy_expired,
				[&tcp]
				{
					error_code ignored_error;
					tcp->socket().close(ignored_error);
				});
			auto [write_error, written] = co_await asio::async_write(
				tcp->socket(), asio::buffer(request),
				asio::as_tuple(asio::use_awaitable));
			(void)written;
			std::string response;
			size_t read_size = 0;
			error_code read_error;
			if( not write_error )
			{
				auto read = co_await asio::async_read_until(tcp->socket(),
					asio::dynamic_buffer(response, 64U * 1024U),
					"\r\n\r\n", asio::as_tuple(asio::use_awaitable));
				read_error = std::get<0>(read);
				read_size = std::get<1>(read);
			}
			proxy_timer.cancel();
			if( proxy_expired )
				co_return sys_unexpected(asio::error::make_error_code(
					asio::error::timed_out));
			if( write_error ) co_return sys_unexpected(write_error);
			if( read_error ) co_return sys_unexpected(read_error);
			(void)read_size;

			const auto first = response.find(' ');
			const auto second = first == std::string::npos ?
				std::string::npos : response.find(' ', first + 1);
			if( first == std::string::npos or
				second == std::string::npos )
				co_return sys_unexpected(make_error_code(
					errc::connection_protocol_error));
			const auto code_text = std::string_view(response).substr(
				first + 1, second - first - 1);
			uint16_t code = 0;
			const auto parsed_code = std::from_chars(code_text.data(),
				code_text.data() + code_text.size(), code);
			if( parsed_code.ec != std::errc{} or
				parsed_code.ptr != code_text.data() + code_text.size() or
				code != 200 )
				co_return sys_unexpected(make_error_code(
					errc::negotiation_failed));
		}

		asio::ssl::context context(asio::ssl::context::tls_client);
		if( auto context_error = configure_tls_context(
			context, key.tls, error); context_error )
			co_return sys_unexpected(context_error);

		tls_socket socket(std::move(tcp->socket()), context);
		error_code address_error;
		ignore_unused(asio::ip::make_address(key.host, address_error));
		if( address_error and ::SSL_set_tlsext_host_name(
			socket.native_handle(), key.host.c_str()) != 1 )
			co_return sys_unexpected(make_error_code(
				errc::negotiation_failed));

		if( key.tls.verify_peer )
		{
			socket.set_verify_callback(
				asio::ssl::host_name_verification(key.host), error);
			if( error ) co_return sys_unexpected(error);
		}

		auto offered = alpn_wire(alpn_preference(key));
		if( not offered.empty() and ::SSL_set_alpn_protos(
			socket.native_handle(), offered.data(),
			static_cast<unsigned int>(offered.size())) != 0 )
			co_return sys_unexpected(make_error_code(
				errc::negotiation_failed));

		asio::steady_timer handshake_timer(executor);
		bool handshake_expired = false;
		arm_async_timeout(handshake_timer, connect_timeout, handshake_expired,
			[&socket]
			{
				error_code ignored_error;
				socket.next_layer().close(ignored_error);
			});
		auto [handshake_error] = co_await socket.async_handshake(
			asio::ssl::stream_base::client,
			asio::as_tuple(asio::use_awaitable));
		handshake_timer.cancel();
		if( handshake_expired )
			co_return sys_unexpected(asio::error::make_error_code(
				asio::error::timed_out));
		if( handshake_error )
			co_return sys_unexpected(handshake_error);

		const unsigned char *alpn_data = nullptr;
		unsigned int alpn_size = 0;
		::SSL_get0_alpn_selected(socket.native_handle(),
			&alpn_data, &alpn_size);
		optional<std::string> alpn;
		if( alpn_data and alpn_size != 0 )
		{
			alpn = std::string(reinterpret_cast<const char *>(alpn_data),
				alpn_size);
			auto value = protocol_from_alpn(*alpn);
			if( not value )
				co_return sys_unexpected(make_error_code(
					errc::negotiation_failed));
			selected = *value;
		}
		else
		{
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
			if( key.protocols.requirement ==
				protocol_requirement::require_http2 )
				co_return sys_unexpected(make_error_code(
					errc::negotiation_failed));
# endif
			protocol_policy fallback_policy = make_policy(key);
			fallback_policy.requirement =
				protocol_requirement::require_http1;
			auto fallback = negotiate_protocol(fallback_policy, key);
			if( not fallback )
				co_return sys_unexpected(fallback.error());
			selected = *fallback;
		}

		if( std::ranges::find(key.protocols.preference, *selected) ==
			key.protocols.preference.end() )
			co_return sys_unexpected(make_error_code(
				errc::negotiation_failed));
		if( key.protocols.requirement ==
			protocol_requirement::require_http1 and not http1(*selected) )
			co_return sys_unexpected(make_error_code(
				errc::negotiation_failed));
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( key.protocols.requirement ==
			protocol_requirement::require_http2 and
			*selected != protocol::v2 )
			co_return sys_unexpected(make_error_code(
				errc::negotiation_failed));
# endif

		auto remote = socket.next_layer().remote_endpoint(error);
		if( error ) co_return sys_unexpected(error);
		auto local = socket.next_layer().local_endpoint(error);
		if( error ) co_return sys_unexpected(error);
		auto transport = std::make_shared<tls_connection>(std::move(socket));
		co_return negotiated_transport {
			.stream = std::move(transport),
			.selected_protocol = *selected,
			.remote = remote,
			.local = local,
			.alpn = std::move(alpn)
		};
#else
		co_return sys_unexpected(make_error_code(
			errc::operation_not_supported));
#endif
	}
	catch(const std::bad_alloc &) {
		co_return sys_unexpected(make_error_code(
			errc::operation_not_supported));
	}
	catch(...) {
		co_return sys_unexpected(exception_error(std::current_exception()));
	}
}

class default_connector final : public connector
{
public:
	explicit default_connector(asio::any_io_executor executor) :
		connector(std::move(executor))
	{
	}

	[[nodiscard]] sys_expected<negotiated_transport> connect(
		const session_key &key, const client_config &config) noexcept override
	{
		try {
			auto selected = negotiate_protocol(make_policy(key), key);
			if( not selected ) return sys_unexpected(selected.error());

			error_code error;
			auto proxy = parse_proxy(key);
			if( key.proxy.endpoint and not proxy )
				return sys_unexpected(make_error_code(errc::invalid_target));

			const auto connect_host = proxy ? proxy->host : key.host;
			const auto connect_port = proxy ? proxy->port : key.port;
			auto endpoints = resolve_with_timeout(m_executor, connect_host,
				connect_port, config.timeout.connect, error);
			if( error ) return sys_unexpected(error);
			if( endpoints.empty() )
				return sys_unexpected(asio::error::make_error_code(
					asio::error::host_not_found));

			auto tcp = std::make_shared<tcp_connection>(m_executor);
			connect_with_timeout(tcp->socket(), endpoints,
				config.timeout.connect, error);
			if( error ) return sys_unexpected(error);
			tcp->socket().set_option(asio::ip::tcp::no_delay(true), error);
			if( error ) return sys_unexpected(error);

			if( key.scheme != "https" )
			{
				auto remote = tcp->remote_endpoint(error);
				if( error ) return sys_unexpected(error);
				auto local = tcp->local_endpoint(error);
				if( error ) return sys_unexpected(error);
				return negotiated_transport {
					.stream = std::move(tcp),
					.selected_protocol = *selected,
					.remote = remote,
					.local = local,
					.absolute_form = proxy.has_value()
				};
			}

#if RIWO_OPENSSL_SUPPORT
			if( proxy )
			{
				const auto authority = origin_authority(key);
				const std::string request = "CONNECT " + authority +
					" HTTP/1.1\r\nHost: " + authority + "\r\n\r\n";
				tcp->set_read_timeout(config.timeout.connect);
				tcp->set_write_timeout(config.timeout.connect);
				(void)tcp->write_sync(asio::buffer(request), error);
				if( error ) return sys_unexpected(error);

				std::string response;
				std::array<char,4096> buffer {};
				while( response.find("\r\n\r\n") == std::string::npos )
				{
					const auto read = tcp->read_some_sync(riwo::buffer(buffer),
						error);
					if( error ) return sys_unexpected(error);
					if( read == 0 )
						return sys_unexpected(make_error_code(
							errc::negotiation_failed));
					response.append(buffer.data(), read);
					if( response.size() > 64U * 1024U )
						return sys_unexpected(make_error_code(
							errc::header_limit_exceeded));
				}
				auto line_end = response.find("\r\n");
				auto first = response.find(' ');
				auto second = first == std::string::npos ? std::string::npos :
					response.find(' ', first + 1);
				if( first == std::string::npos or second == std::string::npos )
					return sys_unexpected(make_error_code(
						errc::connection_protocol_error));
				uint16_t code = 0;
				const auto code_text = std::string_view(response).substr(
					first + 1, second - first - 1);
				const auto parsed_code = std::from_chars(code_text.data(),
					code_text.data() + code_text.size(), code);
				if( parsed_code.ec != std::errc{} or
					parsed_code.ptr != code_text.data() + code_text.size() or
					code != 200 )
					return sys_unexpected(make_error_code(
						errc::negotiation_failed));
				(void)line_end;
				tcp->set_read_timeout(std::nullopt);
				tcp->set_write_timeout(std::nullopt);
			}

			asio::ssl::context context(asio::ssl::context::tls_client);
			if( auto context_error = configure_tls_context(
				context, key.tls, error); context_error )
				return sys_unexpected(context_error);

			tls_socket socket(std::move(tcp->socket()), context);
			error_code address_error;
			ignore_unused(asio::ip::make_address(key.host, address_error));
			if( address_error and ::SSL_set_tlsext_host_name(
				socket.native_handle(), key.host.c_str()) != 1 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));

			if( key.tls.verify_peer )
			{
				socket.set_verify_callback(
					asio::ssl::host_name_verification(key.host), error);
				if( error ) return sys_unexpected(error);
			}

			auto offered = alpn_wire(alpn_preference(key));
			if( not offered.empty() and ::SSL_set_alpn_protos(
				socket.native_handle(), offered.data(),
				static_cast<unsigned int>(offered.size())) != 0 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));

			tls_handshake_with_timeout(socket, config.timeout.connect, error);
			if( error ) return sys_unexpected(error);

			const unsigned char *alpn_data = nullptr;
			unsigned int alpn_size = 0;
			::SSL_get0_alpn_selected(socket.native_handle(),
				&alpn_data, &alpn_size);
			optional<std::string> alpn;
			if( alpn_data and alpn_size != 0 )
			{
				alpn = std::string(reinterpret_cast<const char *>(alpn_data),
					alpn_size);
				auto value = protocol_from_alpn(*alpn);
				if( not value )
					return sys_unexpected(make_error_code(
						errc::negotiation_failed));
				selected = *value;
			}
			else
			{
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
				if( key.protocols.requirement ==
					protocol_requirement::require_http2 )
					return sys_unexpected(make_error_code(
						errc::negotiation_failed));
# endif
				protocol_policy fallback_policy = make_policy(key);
				fallback_policy.requirement =
					protocol_requirement::require_http1;
				auto fallback = negotiate_protocol(fallback_policy, key);
				if( not fallback ) return sys_unexpected(fallback.error());
				selected = *fallback;
			}

			if( std::ranges::find(key.protocols.preference, *selected) ==
				key.protocols.preference.end() )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
			if( key.protocols.requirement ==
				protocol_requirement::require_http1 and not http1(*selected) )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
			if( key.protocols.requirement ==
				protocol_requirement::require_http2 and
				*selected != protocol::v2 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
# endif

			auto remote = socket.next_layer().remote_endpoint(error);
			if( error ) return sys_unexpected(error);
			auto local = socket.next_layer().local_endpoint(error);
			if( error ) return sys_unexpected(error);
			auto transport = std::make_shared<tls_connection>(
				std::move(socket));
			return negotiated_transport {
				.stream = std::move(transport),
				.selected_protocol = *selected,
				.remote = remote,
				.local = local,
				.alpn = std::move(alpn)
			};
#else
			return sys_unexpected(make_error_code(
				errc::operation_not_supported));
#endif
		}
		catch(const std::bad_alloc &) {
			return sys_unexpected(make_error_code(
				errc::operation_not_supported));
		}
		catch(...) {
			return sys_unexpected(exception_error(std::current_exception()));
		}
	}

	void connect_async(const session_key &key, const client_config &config,
		connect_handler handler) override
	{
		auto executor = m_executor;
		asio::co_spawn(executor, connect_async_impl(executor, key, config),
		[handler = std::move(handler)](std::exception_ptr exception,
			sys_expected<negotiated_transport> result) mutable
		{
			if( exception )
			{
				std::move(handler)(exception_error(exception),
					negotiated_transport{});
				return;
			}
			if( result )
				std::move(handler)(error_code{}, std::move(*result));
			else
				std::move(handler)(result.error(), negotiated_transport{});
		});
	}
};

class default_session_factory final : public session_factory
{
public:
	sys_expected<std::shared_ptr<client_session>> create(
		const session_key &key, negotiated_transport transport,
		const client_config &config) override
	{
		if( not transport.stream )
			return sys_unexpected(make_error_code(errc::negotiation_failed));
	#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( transport.selected_protocol == protocol::v2 )
			return h2::make_client_session(std::move(transport.stream), config,
				transport.remote, transport.local);
	#endif
		if( not http1(transport.selected_protocol) )
			return sys_unexpected(make_error_code(
				errc::operation_not_supported));
		ignore_unused(key);
		auto executor = transport.stream->get_executor();
		auto selected = transport.selected_protocol;
		auto limits = config.response_limits;
		return h1::make_client_session(std::move(executor), selected,
			std::move(limits), std::move(transport.stream), config.timeout,
			transport.absolute_form);
	}
};

} //namespace

size_t session_key_hash::operator()(const session_key &value) const noexcept
{
	size_t seed = std::hash<std::string>{}(value.scheme);
	mix(seed, std::hash<std::string>{}(value.host));
	mix(seed, std::hash<uint16_t>{}(value.port));
	if( value.proxy.endpoint )
		mix(seed, std::hash<std::string>{}(*value.proxy.endpoint));
	else
		mix(seed, 1);
	mix(seed, std::hash<bool>{}(value.tls.enabled));
	mix(seed, std::hash<bool>{}(value.tls.verify_peer));
	mix(seed, std::hash<std::string>{}(
		value.tls.certificate_authority_file));
	for( const auto protocol_value : value.protocols.preference )
		mix(seed, static_cast<size_t>(protocol_value) + 17U);
	mix(seed, static_cast<size_t>(value.protocols.requirement) + 31U);
	if( value.local_bind )
		mix(seed, std::hash<std::string>{}(value.local_bind->address().to_string()));
	else
		mix(seed, 3);
	return seed;
}

session_key make_session_key(const request_head &head,
	const client_config &config)
{
	const auto scheme = lower(head.scheme.empty() ?
		std::string_view(head.url.protocol()) : std::string_view(head.scheme));
	uint16_t port = head.url.port();
	if( port == 0 )
		port = scheme == "https" ? 443 : 80;

	session_key result;
	result.scheme = scheme;
	result.host = lower(head.url.host());
	result.port = port;
	if( config.proxy.forward_proxy )
		result.proxy.endpoint = config.proxy.forward_proxy->to_string();
	if( scheme == "https" )
		result.tls = {
			.enabled = true,
			.verify_peer = config.tls.verify_peer,
			.certificate_authority_file =
				config.tls.certificate_authority_file ?
					config.tls.certificate_authority_file->string() :
					std::string{}
		};
	result.protocols = {
		.preference = config.protocols.preference,
		.requirement = config.protocols.requirement
	};
	return result;
}

connector::connector(executor_type executor) :
	m_executor(std::move(executor))
{
}

connector::~connector() = default;

auto connector::get_executor() const noexcept -> executor_type {
	return m_executor;
}

void connector::connect_async(const session_key &key,
	const client_config &config, connect_handler handler)
{
	auto result = connect(key, config);
	auto executor = this->get_executor();
	asio::post(executor,
	[result = std::move(result), handler = std::move(handler)]() mutable {
		if( result ) std::move(handler)(error_code{}, std::move(*result));
		else std::move(handler)(result.error(), negotiated_transport{});
	});
}

connector_ptr make_default_connector(asio::any_io_executor executor)
{
	return std::make_shared<default_connector>(std::move(executor));
}

session_factory_ptr make_default_session_factory()
{
	return std::make_shared<default_session_factory>();
}

sys_expected<protocol> negotiate_protocol(const protocol_policy &policy,
	const session_key &key, bool tls_available)
{
	protocol_policy_identity identity {
		.preference = policy.preference,
		.requirement = policy.requirement
	};
	if( not valid_policy(identity) )
		return sys_unexpected(make_error_code(errc::negotiation_failed));

	if( key.scheme == "https" and not tls_available )
		return sys_unexpected(make_error_code(errc::operation_not_supported));
	if( key.scheme != "http" and key.scheme != "https" )
		return sys_unexpected(make_error_code(errc::invalid_target));

	std::vector<protocol> eligible;
	for( const auto value : policy.preference )
	{
		if( value == protocol::v1_0 )
		{
			if( key.scheme == "http" ) eligible.push_back(value);
			continue;
		}
		if( value == protocol::v1_1 )
		{
			eligible.push_back(value);
			continue;
		}
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( value == protocol::v2 )
		{
			if( policy.requirement == protocol_requirement::require_http2 or
				key.scheme == "https" )
				eligible.push_back(value);
			continue;
		}
#endif
	}

	if( policy.requirement == protocol_requirement::require_http1 )
	{
		std::erase_if(eligible, [](protocol value) { return not http1(value); });
	}
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	else if( policy.requirement == protocol_requirement::require_http2 )
	{
		std::erase_if(eligible, [](protocol value) {
			return value != protocol::v2;
		});
	}
#endif
	if( eligible.empty() )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	return eligible.front();
}

} //namespace riwo::http_exp::detail
