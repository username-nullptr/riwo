// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/connector.h"
#include "detail/factory.h"
#include <riwo/http_exp/protocol/detail/h1/session.h>
#include <riwo/http_exp/transport/tcp_connection.h>

#if RIWO_OPENSSL_SUPPORT
# include <riwo/http_exp/transport/tls_connection.h>
# include <openssl/ssl.h>
#endif

namespace riwo::http_exp::detail { namespace
{

namespace htransport = ::riwo::http_exp::transport;

[[nodiscard]] std::string lower(std::string_view text)
{
	std::string result(text);
	std::ranges::transform(result, result.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return result;
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
	return value == protocol::v1_0 || value == protocol::v1_1;
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
	if( !identity.verify_peer ) return {};

	if( identity.certificate_authority_file.empty() )
		context.set_default_verify_paths(error);
	else
		context.load_verify_file(identity.certificate_authority_file, error);
	return error;
}
#endif

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
			ignore_unused(config);
			auto selected = negotiate_protocol(make_policy(key), key);
			if( !selected ) return sys_unexpected(selected.error());

			asio::ip::tcp::resolver resolver(m_executor);
			error_code error;
			auto endpoints = resolver.resolve(key.host, std::to_string(key.port),
				error);
			if( error ) return sys_unexpected(error);
			if( endpoints.empty() )
				return sys_unexpected(asio::error::make_error_code(
					asio::error::host_not_found));

			if( key.scheme != "https" )
			{
				auto transport =
					std::make_shared<htransport::tcp_connection>(m_executor);
				asio::connect(transport->socket(), endpoints, error);
				if( error ) return sys_unexpected(error);
				transport->socket().set_option(asio::ip::tcp::no_delay(true),
					error);
				if( error ) return sys_unexpected(error);
				auto remote = transport->remote_endpoint(error);
				if( error ) return sys_unexpected(error);
				auto local = transport->local_endpoint(error);
				if( error ) return sys_unexpected(error);
				return negotiated_transport {
					.stream = std::move(transport),
					.selected_protocol = *selected,
					.remote = remote,
					.local = local
				};
			}

			if( key.scheme != "https" )
				return sys_unexpected(make_error_code(
					errc::operation_not_supported));
#if RIWO_OPENSSL_SUPPORT
			asio::ssl::context context(asio::ssl::context::tls_client);
			if( auto context_error = configure_tls_context(
				context, key.tls, error); context_error )
				return sys_unexpected(context_error);

			htransport::tls_socket socket(m_executor, context);
			error_code address_error;
			ignore_unused(asio::ip::make_address(key.host, address_error));
			if( address_error && ::SSL_set_tlsext_host_name(
				socket.native_handle(), key.host.c_str()) != 1 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));

			if( key.tls.verify_peer )
			{
				socket.set_verify_callback(
					asio::ssl::host_name_verification(key.host), error);
				if( error ) return sys_unexpected(error);
			}

			auto offered = alpn_wire(key.protocols.preference);
			if( !offered.empty() && ::SSL_set_alpn_protos(
				socket.native_handle(), offered.data(),
				static_cast<unsigned int>(offered.size())) != 0 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));

			asio::connect(socket.next_layer(), endpoints, error);
			if( error ) return sys_unexpected(error);
			socket.next_layer().set_option(asio::ip::tcp::no_delay(true),
				error);
			if( error ) return sys_unexpected(error);

			socket.handshake(asio::ssl::stream_base::client, error);
			if( error ) return sys_unexpected(error);

			const unsigned char *alpn_data = nullptr;
			unsigned int alpn_size = 0;
			::SSL_get0_alpn_selected(socket.native_handle(),
				&alpn_data, &alpn_size);
			optional<std::string> alpn;
			if( alpn_data && alpn_size != 0 )
			{
				alpn = std::string(reinterpret_cast<const char *>(alpn_data),
					alpn_size);
				auto value = protocol_from_alpn(*alpn);
				if( !value )
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
				if( !fallback ) return sys_unexpected(fallback.error());
				selected = *fallback;
			}

			if( std::ranges::find(key.protocols.preference, *selected) ==
				key.protocols.preference.end() )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
			if( key.protocols.requirement ==
				protocol_requirement::require_http1 && !http1(*selected) )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
# if RIWO_HTTP_EXP_HTTP2_SUPPORT
			if( key.protocols.requirement ==
				protocol_requirement::require_http2 &&
				*selected != protocol::v2 )
				return sys_unexpected(make_error_code(
					errc::negotiation_failed));
# endif

			auto native = socket.next_layer().remote_endpoint(error);
			if( error ) return sys_unexpected(error);
			auto local = socket.next_layer().local_endpoint(error);
			if( error ) return sys_unexpected(error);
			auto transport = std::make_shared<htransport::tls_connection>(
				std::move(socket));
			return negotiated_transport {
				.stream = std::move(transport),
				.selected_protocol = *selected,
				.remote = native,
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
};

class default_session_factory final : public session_factory
{
public:
	sys_expected<std::shared_ptr<client_session>> create(
		const session_key &key, negotiated_transport transport,
		const client_config &config) override
	{
		if( !transport.stream )
			return sys_unexpected(make_error_code(errc::negotiation_failed));
		if( !http1(transport.selected_protocol) )
			return sys_unexpected(make_error_code(
				errc::operation_not_supported));
		ignore_unused(key);
		auto executor = transport.stream->get_executor();
		auto selected = transport.selected_protocol;
		auto limits = config.response_limits;
		return h1::make_client_session(std::move(executor), selected,
			std::move(limits), std::move(transport.stream));
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
	if( !valid_policy(identity) )
		return sys_unexpected(make_error_code(errc::negotiation_failed));

	if( key.scheme == "https" && !tls_available )
		return sys_unexpected(make_error_code(errc::operation_not_supported));
	if( key.scheme != "http" && key.scheme != "https" )
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
			if( policy.requirement == protocol_requirement::require_http2 ||
				key.scheme == "https" )
				eligible.push_back(value);
			continue;
		}
#endif
	}

	if( policy.requirement == protocol_requirement::require_http1 )
	{
		std::erase_if(eligible, [](protocol value) { return !http1(value); });
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
