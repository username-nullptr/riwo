// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/http_exp/session/v1/session.h>
#include <riwo/http_exp/session/v2/session.h>
#include <riwo/http_exp/transport/preface.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace riwo::http_exp::detail
{

namespace
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
	if( policy.requirement == protocol_requirement::require_v2 )
		return false;
#endif
	return contains(policy, protocol::v1_0) or
		contains(policy, protocol::v1_1);
}

class RIWO_DECL_HIDDEN v1_server_loop final :
	public std::enable_shared_from_this<v1_server_loop>
{
	RIWO_DISABLE_COPY_MOVE(v1_server_loop)

public:
	v1_server_loop() = default;

	server_session_ptr session {};
	server_accept_handler_t handler {};
	bool served = false;

	void start()
	{
		auto self = shared_from_this();
		session->accept(
		[self = std::move(self)](const error_code &error,
			session_stream_ptr value) mutable {
			if( error )
			{
				if( not self->served )
					self->handler(error, {}, {});
				return ;
			}
			self->served = true;

			auto request = value ? value->request() : request_head{};
			self->handler(error_code{}, std::move(value),
				std::move(request));
		});
	}
};

using v1_server_loop_ptr = std::shared_ptr<v1_server_loop>;

class RIWO_DECL_HIDDEN v1_server_connection final : public server_connection
{
	RIWO_DISABLE_COPY_MOVE(v1_server_connection)

public:
	explicit v1_server_connection(v1_server_loop_ptr loop) :
		m_loop(std::move(loop)) {}

	void cancel() noexcept override {
		m_loop->session->graceful_shutdown([](error_code) {});
	}
	[[nodiscard]] bool closed() const noexcept override {
		return m_loop->session->closed();
	}

private:
	v1_server_loop_ptr m_loop {};
};

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
[[nodiscard]] bool permits_http2(const protocol_policy &policy) noexcept
{
	if( policy.requirement == protocol_requirement::require_v1 )
		return false;
	return contains(policy, protocol::v2);
}

class v2_server_loop final : public std::enable_shared_from_this<v2_server_loop>
{
public:
	server_session_ptr session {};
	server_accept_handler_t handler {};
	bool served = false;

	void start()
	{
		auto self = shared_from_this();
		session->accept(
		[self = std::move(self)](error_code error, session_stream_ptr value) mutable {
			if( error )
			{
				const auto benign =
					error == asio::error::make_error_code(asio::error::eof) or
					error == asio::error::make_error_code(
						asio::error::connection_reset) or
					error == asio::error::make_error_code(
						asio::error::operation_aborted);
				if( not self->served or not benign )
					self->handler(error, {}, {});
				return ;
			}
			self->served = true;
			auto request = value ? value->request() : request_head{};
			self->handler(error_code{}, std::move(value), std::move(request));
			self->start();
		});
	}
};

class v2_server_connection final : public server_connection
{
public:
	explicit v2_server_connection(std::shared_ptr<v2_server_loop> loop) :
		m_loop(std::move(loop)) {}

	void cancel() noexcept override {
		m_loop->session->graceful_shutdown([](error_code) {});
	}
	[[nodiscard]] bool closed() const noexcept override {
		return m_loop->session->closed();
	}

private:
	std::shared_ptr<v2_server_loop> m_loop;
};

[[nodiscard]] server_connection_ptr_t accept_h2(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input,
	server_accept_handler_t handler)
{
	if( auto error = validate_server_config(config) )
	{
		std::move(handler)(error, {}, {});
		return {};
	}
	auto made = v2::make_server_session(std::move(transport), {
		.timeout = std::move(config.timeout),
		.request_limits = std::move(config.request_limits),
		.h2 = std::move(config.h2)
	}, std::move(initial_input));
	if( not made )
	{
		std::move(handler)(made.error(), {}, {});
		return {};
	}
	auto loop = std::make_shared<v2_server_loop>();
	loop->session = std::move(*made);
	loop->handler = std::move(handler);
	loop->start();
	return std::make_shared<v2_server_connection>(std::move(loop));
}
#endif

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
				if( error )
					return self->finish_error(error);
				if( result == preface_result::v2 )
				{
					self->m_selected =
						accept_h2(self->m_transport,
						self->m_config, std::move(prefix),
						std::move(self->m_handler));
					return ;
				}
				if( not h1 )
					return self->finish_error(
						make_error_code(errc::negotiation_failed));
				self->m_selected = accept_h1(self->m_transport,
					self->m_config.request_limits, self->m_config.timeout,
					std::move(self->m_handler), std::move(prefix));
			});
			return ;
		}
#endif
		if( h1 )
		{
			m_selected = accept_h1(m_transport, m_config.request_limits,
				m_config.timeout, std::move(m_handler));
			return ;
		}
		finish_error(make_error_code(errc::negotiation_failed));
	}

	void cancel() noexcept override
	{
		if( m_selected )
			m_selected->cancel();
		else
			m_transport->cancel();
	}

	[[nodiscard]] bool closed() const noexcept override
	{
		return m_selected ? m_selected->closed() : not m_transport->is_open();
	}

private:
	void finish_error(error_code error)
	{
		m_transport->cancel();
		if( m_handler )
			std::move(m_handler)(error, {}, {});
	}

	std::shared_ptr<::riwo::http_exp::connection> m_transport;
	server_config m_config;
	server_accept_handler_t m_handler;
	v2_preface_detector m_detector;
	server_connection_ptr_t m_selected;
};

} //namespace

server_connection_ptr_t accept_h1(asio::ip::tcp::socket socket,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler_t handler)
{
	error_code option_error;
	(void)socket.set_option(asio::ip::tcp::no_delay(true), option_error);
	return accept_h1(std::make_shared<tcp_connection>(std::move(socket)),
		std::move(limits), timeouts, std::move(handler));
}

server_connection_ptr_t accept_h1(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler_t handler, std::string initial_input)
{
	auto loop = std::make_shared<v1_server_loop>();
	loop->handler = std::move(handler);
	std::weak_ptr<v1_server_loop> weak = loop;
	loop->session = v1::make_server_session(
		std::move(transport), protocol::v1_1, std::move(limits), timeouts,
		std::move(initial_input),
		[weak](bool reusable) {
			if( not reusable )
				return ;
			if( auto self = weak.lock() )
				self->start();
		});
	loop->start();
	return std::make_shared<v1_server_connection>(std::move(loop));
}

error_code validate_server_config(const server_config &config) noexcept
{
	if( config.h1.max_pipeline_depth != 1 or
		config.protocols.preference.empty() )
		return make_error_code(config.h1.max_pipeline_depth != 1 ?
			errc::operation_not_supported : errc::negotiation_failed);
	for(auto current=config.protocols.preference.begin();
		current!=config.protocols.preference.end(); ++current)
		if( std::ranges::find(config.protocols.preference.begin(), current,
			*current) != current )
			return make_error_code(errc::negotiation_failed);

	if( not permits_http1(config.protocols)
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		and not permits_http2(config.protocols)
#endif
	)
		return make_error_code(errc::negotiation_failed);
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
		if( handler )
			handler(make_error_code(errc::negotiation_failed), {}, {});
		return {};
	}
	if( auto error = validate_server_config(config) )
	{
		if( handler )
			handler(error, {}, {});
		return {};
	}
	if( selected_protocol == protocol::v1_0 or
		selected_protocol == protocol::v1_1 )
	{
		if( not permits_http1(config.protocols) )
		{
			if( handler )
				handler(make_error_code(errc::negotiation_failed), {}, {});
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
			if( handler )
				handler(make_error_code(errc::negotiation_failed), {}, {});
			return {};
		}
		return accept_h2(
			std::move(transport), std::move(config), {},
			std::move(handler));
	}
#endif
	if( handler )
		handler(make_error_code(errc::negotiation_failed), {}, {});
	return {};
}

} //namespace riwo::http_exp::detail
