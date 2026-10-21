// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H

#include <riwo/http_exp/client/detail/connector.h>
#include <riwo/http_exp/transport/connector.h>
#include <riwo/http_exp/transport/detail/byte_stream_connection.h>

namespace riwo::http_exp::detail
{

template<core_concepts::exec Exec>
class connector_adapter final : public connector
{
public:
	explicit connector_adapter(
		std::shared_ptr<basic_transport_connector<Exec>> value) :
		connector_adapter(asio::any_io_executor(riwo::get_executor()),
			std::move(value))
	{
	}

	connector_adapter(asio::any_io_executor executor,
		std::shared_ptr<basic_transport_connector<Exec>> value) :
		connector(std::move(executor)), m_value(std::move(value))
	{
	}

	[[nodiscard]] sys_expected<detail::negotiated_transport> connect(
		const session_key &key, const client_config &config) noexcept override
	{
		if( not m_value )
			return sys_unexpected(make_error_code(errc::operation_not_permitted));
		auto result = m_value->connect_sync(make_request(key, config));
		if( not result ) return sys_unexpected(result.error());
		return convert(std::move(*result));
	}

	void connect_async(const session_key &key, const client_config &config,
		connect_handler_t handler) override
	{
		if( not m_value )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				detail::negotiated_transport{});
			return;
		}
		m_value->connect(make_request(key, config),
			[handler = std::move(handler)](error_code error,
				basic_negotiated_transport<Exec> result) mutable
			{
				if( error )
				{
					std::move(handler)(error, detail::negotiated_transport{});
					return;
				}
				try { std::move(handler)(error_code{}, convert(std::move(result))); }
				catch(...) { std::move(handler)(exception_error(
					std::current_exception()), detail::negotiated_transport{}); }
			});
	}

	~connector_adapter() override {
		if( m_value ) m_value->cancel();
	}

private:
	[[nodiscard]] static connect_request make_request(const session_key &key,
		const client_config &config)
	{
		connect_request result;
		result.scheme = key.scheme;
		result.host = key.host;
		result.port = key.port;
		result.protocols = config.protocols;
		result.proxy = config.proxy;
		result.tls = config.tls;
		result.deadline = config.timeout.connect;
		if( key.local_bind ) result.local_bind =
			key.local_bind->address().to_string() + ':' +
			std::to_string(key.local_bind->port());
		return result;
	}

	[[nodiscard]] static detail::negotiated_transport convert(
		basic_negotiated_transport<Exec> value)
	{
		auto metadata = value.metadata;
		bool absolute = false;
		if( auto found = metadata.attributes.find("http.absolute_form");
			found != metadata.attributes.end() )
			absolute = found->second.to_bool().value_or(false);
		auto selected = value.selected_protocol;
		auto alpn = metadata.alpn;
		return detail::negotiated_transport {
			.stream = make_connection(std::move(value)),
			.selected_protocol = selected,
			.alpn = std::move(alpn),
			.absolute_form = absolute
		};
	}

	std::shared_ptr<basic_transport_connector<Exec>> m_value;
};

template<core_concepts::exec Exec>
[[nodiscard]] connector_ptr_t make_connector_adapter(
	std::shared_ptr<basic_transport_connector<Exec>> connector)
{
	return std::make_shared<connector_adapter<Exec>>(std::move(connector));
}

template<core_concepts::exec Exec>
[[nodiscard]] connector_ptr_t make_connector_adapter(
	asio::any_io_executor executor,
	std::shared_ptr<basic_transport_connector<Exec>> connector)
{
	return std::make_shared<connector_adapter<Exec>>(std::move(executor),
		std::move(connector));
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_ADAPTER_H
