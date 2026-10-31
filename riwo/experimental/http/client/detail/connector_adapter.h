// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H
#define RIWO_EXP_HTTP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H

#include <riwo/experimental/http/client/detail/connector.h>
#include <riwo/experimental/http/transport/connector.h>
#include <riwo/experimental/http/transport/detail/byte_stream_connection.h>

namespace riwo::experimental::http::detail
{

template<core_concepts::exec Exec>
class RIWO_DECL_HIDDEN connector_adapter final : public connector
{
	RIWO_DISABLE_COPY_MOVE(connector_adapter)

public:
	using transport_connector_t = basic_transport_connector<Exec>;
	using transport_connector_ptr = std::shared_ptr<transport_connector_t>;

	using negotiated_transport_t = basic_negotiated_transport<Exec>;

public:
	explicit connector_adapter(transport_connector_ptr value) :
		connector_adapter(asio::any_io_executor(riwo::get_executor()), std::move(value)) {}

	connector_adapter
	(asio::any_io_executor executor, transport_connector_ptr value) :
		connector(std::move(executor)), m_value(std::move(value)) {}

	[[nodiscard]] sys_expected<negotiated_transport> connect
	(const session_key &key, const client_config &config) noexcept override
	{
		if( not m_value )
			return sys_unexpected(make_error_code(errc::operation_not_permitted));

		auto result = m_value->connect_sync(make_request(key, config));
		if( not result )
			return sys_unexpected(result.error());

		return convert(std::move(*result));
	}

	void connect_async
	(const session_key &key, const client_config &config, connect_handler_t handler) override
	{
		if( not m_value )
		{
			std::move(handler) (
				make_error_code(errc::operation_not_permitted),
				negotiated_transport {}
			);
			return ;
		}
		m_value->connect(make_request(key, config),
		[handler = std::move(handler)](error_code error, negotiated_transport_t result) mutable
		{
			if( error )
			{
				std::move(handler)(error, negotiated_transport{});
				return ;
			}
			try {
				std::move(handler)(error_code{}, convert(std::move(result)));
			}
			catch(...)
			{
				std::move(handler) (
					exception_error(std::current_exception()),
					negotiated_transport {}
				);
			}
		});
	}

	~connector_adapter() override
	{
		if( m_value )
			m_value->cancel();
	}

private:
	[[nodiscard]] static connect_request
	make_request(const session_key &key, const client_config &config)
	{
		connect_request result {};
		result.scheme = key.scheme;

		result.host = key.host;
		result.port = key.port;

		result.protocols = config.protocols;

		result.proxy = config.proxy;
		result.proxy.forward_proxy.reset();

		result.proxy.use_environment = false;
		result.proxy.no_proxy.clear();

		if( key.proxy.endpoint )
			result.proxy.forward_proxy = url(*key.proxy.endpoint);

		result.proxy.type = key.proxy.type;
		result.proxy.authorization = key.proxy.authorization;

		result.proxy.username = key.proxy.username;
		result.proxy.password = key.proxy.password;

		result.tls = config.tls;
		result.deadline = config.timeout.connect;

		if( key.local_bind )
		{
			result.local_bind = key.local_bind->address().to_string() + ':' +
				std::to_string(key.local_bind->port());
		}
		return result;
	}

	[[nodiscard]] static negotiated_transport convert(negotiated_transport_t value)
	{
		auto metadata = value.metadata;
		bool absolute = false;

		if( auto found = metadata.attributes.find("http.absolute_form");
			found != metadata.attributes.end() )
			absolute = found->second.to_bool().value_or(false);

		auto selected = value.selected_protocol;
		auto alpn = metadata.alpn;
		auto stream = make_connection(std::move(value));

		endpoint_t remote {};
		endpoint_t local {};

		if( stream )
		{
			error_code endpoint_error {};
			remote = stream->remote_endpoint(endpoint_error);

			endpoint_error.clear();
			local = stream->local_endpoint(endpoint_error);
		}
		return negotiated_transport {
			.stream = std::move(stream),
			.selected_protocol = selected,
			.remote = remote,
			.local = local,
			.alpn = std::move(alpn),
			.absolute_form = absolute
		};
	}

private:
	transport_connector_ptr m_value {};
};

template<core_concepts::exec Exec>
[[nodiscard]] RIWO_DECL_HIDDEN connector_ptr
make_connector_adapter(std::shared_ptr<basic_transport_connector<Exec>> connector) {
	return std::make_shared<connector_adapter<Exec>>(std::move(connector));
}

template<core_concepts::exec Exec>
[[nodiscard]] RIWO_DECL_HIDDEN connector_ptr make_connector_adapter
(asio::any_io_executor executor, std::shared_ptr<basic_transport_connector<Exec>> connector)
{
	return std::make_shared<connector_adapter<Exec>>(
		std::move(executor), std::move(connector)
	);
}

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H
