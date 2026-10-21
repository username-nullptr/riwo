// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/http_exp/protocol/v2/detail/session.h>
#include <riwo/http_exp/transport/preface.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace riwo::http_exp::detail { namespace
{

[[nodiscard]] bool contains(const protocol_policy &policy,
	protocol value) noexcept
{
	return std::ranges::find(policy.preference, value) !=
		policy.preference.end();
}

[[nodiscard]] bool permits_http1(const protocol_policy &policy) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( policy.requirement == protocol_requirement::require_http2 ) return false;
#endif
	return contains(policy, protocol::v1_0) or
		contains(policy, protocol::v1_1);
}

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
[[nodiscard]] bool permits_http2(const protocol_policy &policy) noexcept
{
	if( policy.requirement == protocol_requirement::require_http1 ) return false;
	return contains(policy, protocol::v2);
}
#endif

class replay_connection final : public ::riwo::http_exp::connection,
	public std::enable_shared_from_this<replay_connection>
{
public:
	replay_connection(std::shared_ptr<::riwo::http_exp::connection> next,
		std::string prefix) : m_next(std::move(next)), m_prefix(std::move(prefix)) {}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_next->get_executor();
	}

	void read_some(mutable_buffer buffer, io_handler_t handler) override
	{
		if( m_offset == m_prefix.size() )
			return m_next->read_some(buffer, std::move(handler));
		const auto count = copy_prefix(buffer);
		asio::post(get_executor(),
		[handler = std::move(handler), count]() mutable {
			std::move(handler)(error_code{}, count);
		});
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		if( m_offset != m_prefix.size() ) return copy_prefix(buffer);
		return m_next->read_some_sync(buffer, error);
	}

	void write(const_buffer buffer, io_handler_t handler) override {
		m_next->write(buffer, std::move(handler));
	}
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override {
		return m_next->write_sync(buffer, error);
	}
	void close(close_handler_t handler) override {
		m_next->close(std::move(handler));
	}
	void cancel() noexcept override { m_next->cancel(); }
	void set_read_timeout(optional<timeout_t> value) override {
		m_next->set_read_timeout(value);
	}
	void set_write_timeout(optional<timeout_t> value) override {
		m_next->set_write_timeout(value);
	}
	[[nodiscard]] bool is_open() const noexcept override {
		return m_next->is_open();
	}
	[[nodiscard]] endpoint_t remote_endpoint(
		error_code &error) const noexcept override {
		return m_next->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_t local_endpoint(
		error_code &error) const noexcept override {
		return m_next->local_endpoint(error);
	}

private:
	[[nodiscard]] size_t copy_prefix(mutable_buffer buffer) noexcept
	{
		const auto count = std::min(buffer.size(), m_prefix.size() - m_offset);
		if( count != 0 )
			std::memcpy(buffer.data(), m_prefix.data() + m_offset, count);
		m_offset += count;
		return count;
	}

	std::shared_ptr<::riwo::http_exp::connection> m_next;
	std::string m_prefix;
	size_t m_offset = 0;
};

class pending_server_connection final : public server_connection,
	public std::enable_shared_from_this<pending_server_connection>
{
public:
	pending_server_connection(
		std::shared_ptr<::riwo::http_exp::connection> transport,
		server_config config, server_accept_handler_t handler) :
		m_transport(std::move(transport)), m_config(std::move(config)),
		m_handler(std::move(handler))
	{
	}

	void start()
	{
		const auto h1 = permits_http1(m_config.protocols);
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		const auto allow_h2 = permits_http2(m_config.protocols);
		if( allow_h2 )
		{
			m_transport->set_read_timeout(m_config.timeout.header);
			auto self = shared_from_this();
			m_detector.detect(m_transport,
			[self = std::move(self), h1]
			(error_code error, preface_result result,
				std::string prefix) mutable {
				if( error ) return self->finish_error(error);
				if( result == preface_result::http2 )
				{
					self->m_selected =
						::riwo::http_exp::v2::detail::accept_h2(self->m_transport,
						self->m_config, std::move(prefix),
						std::move(self->m_handler));
					return;
				}
				if( not h1 )
					return self->finish_error(
						make_error_code(errc::negotiation_failed));
				auto replay = std::make_shared<replay_connection>(
					self->m_transport, std::move(prefix));
				self->m_selected = accept_h1(std::move(replay),
					self->m_config.request_limits, self->m_config.timeout,
					std::move(self->m_handler));
			});
			return;
		}
#endif
		if( h1 )
		{
			m_selected = accept_h1(m_transport, m_config.request_limits,
				m_config.timeout, std::move(m_handler));
			return;
		}
		finish_error(make_error_code(errc::negotiation_failed));
	}

	void cancel() noexcept override
	{
		if( m_selected ) m_selected->cancel();
		else m_transport->cancel();
	}

	[[nodiscard]] bool closed() const noexcept override
	{
		return m_selected ? m_selected->closed() : not m_transport->is_open();
	}

private:
	void finish_error(error_code error)
	{
		m_transport->cancel();
		if( m_handler ) std::move(m_handler)(error, {}, {});
	}

	std::shared_ptr<::riwo::http_exp::connection> m_transport;
	server_config m_config;
	server_accept_handler_t m_handler;
	http2_preface_detector m_detector;
	server_connection_ptr_t m_selected;
};

} //namespace

error_code validate_server_config(const server_config &config) noexcept
{
	if( config.h1.max_pipeline_depth != 1 or
		config.protocols.preference.empty() )
		return make_error_code(config.h1.max_pipeline_depth != 1 ?
			errc::operation_not_supported : errc::negotiation_failed);
	for( auto current = config.protocols.preference.begin();
		current != config.protocols.preference.end(); ++current )
		if( std::ranges::find(config.protocols.preference.begin(), current,
			*current) != current )
			return make_error_code(errc::negotiation_failed);

	if( not permits_http1(config.protocols)
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		and not permits_http2(config.protocols)
#endif
	) return make_error_code(errc::negotiation_failed);
	const auto &limits = config.request_limits;
	if( limits.max_start_line_size == 0 or
		limits.max_field_name_size == 0 or
		limits.max_field_value_size == 0 or
		limits.max_header_list_size == 0 or
		limits.max_field_count == 0 or limits.max_buffered_body == 0 or
		limits.max_discard_body == 0 )
		return make_error_code(errc::operation_not_permitted);

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( permits_http2(config.protocols) )
	{
		const auto &value = config.h2;
		constexpr uint32_t maximum_window = 0x7fffffffU;
		if( value.initial_stream_window == 0 or
			value.initial_stream_window > maximum_window or
			value.initial_connection_window < 65'535 or
			value.initial_connection_window > maximum_window or
			value.max_concurrent_streams == 0 or
			value.max_frame_size < 16U * 1024U or
			value.max_frame_size > 16'777'215U or
			value.max_header_list_size == 0 or
			value.max_header_list_size > UINT32_MAX or
			value.max_dynamic_table_size > UINT32_MAX or
			value.inbound_stream_buffer < value.initial_stream_window or
			value.outbound_stream_buffer == 0 or
			value.inbound_connection_buffer <
				value.initial_connection_window or
			value.outbound_connection_buffer == 0 )
			return make_error_code(errc::operation_not_permitted);
		const auto reserved =
			static_cast<uint64_t>(value.initial_stream_window) *
			value.max_concurrent_streams;
		if( reserved > value.inbound_connection_buffer )
			return make_error_code(errc::operation_not_permitted);
	}
#endif
	return {};
}

server_connection_ptr_t accept_server(asio::ip::tcp::socket socket,
	server_config config, server_accept_handler_t handler)
{
	error_code option_error;
	(void)socket.set_option(asio::ip::tcp::no_delay(true), option_error);
	auto value = std::make_shared<pending_server_connection>(
		std::make_shared<tcp_connection>(std::move(socket)),
		std::move(config), std::move(handler));
	value->start();
	return value;
}

server_connection_ptr_t accept_server(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	protocol selected_protocol, server_config config,
	server_accept_handler_t handler)
{
	if( not transport )
	{
		if( handler ) handler(make_error_code(errc::negotiation_failed), {}, {});
		return {};
	}
	if( auto error = validate_server_config(config) )
	{
		if( handler ) handler(error, {}, {});
		return {};
	}
	if( selected_protocol == protocol::v1_0 or
		selected_protocol == protocol::v1_1 )
	{
		if( not permits_http1(config.protocols) )
		{
			if( handler ) handler(make_error_code(errc::negotiation_failed), {}, {});
			return {};
		}
		return accept_h1(std::move(transport), config.request_limits,
			config.timeout, std::move(handler));
	}
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( selected_protocol == protocol::v2 )
	{
		if( not permits_http2(config.protocols) )
		{
			if( handler ) handler(make_error_code(errc::negotiation_failed), {}, {});
			return {};
		}
		return ::riwo::http_exp::v2::detail::accept_h2(
			std::move(transport), std::move(config), {},
			std::move(handler));
	}
#endif
	if( handler ) handler(make_error_code(errc::negotiation_failed), {}, {});
	return {};
}

} //namespace riwo::http_exp::detail
