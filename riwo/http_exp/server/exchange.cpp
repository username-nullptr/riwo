// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"
#include <riwo/http_exp/protocol/detail/session.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_TAPI server_exchange_state::impl
{
public:
	impl(std::shared_ptr<stream> value, request_head metadata) :
		m_stream(std::move(value)), m_request(std::move(metadata)),
		m_request_body(m_stream ? m_stream->inbound_body() : make_body_reader_state()),
		m_response_body(m_stream ? m_stream->outbound_body() : make_body_writer_state())
	{
		if( not m_request_body )
			m_request_body = make_body_reader_state();
		if( not m_response_body )
			m_response_body = make_body_writer_state();
	}

	std::shared_ptr<stream> m_stream;
	request_head m_request;
	std::shared_ptr<body_reader_state> m_request_body;
	std::shared_ptr<body_writer_state> m_response_body;
	bool m_response_pending = false;
	bool m_response_started = false;
	bool m_duplex_taken = false;
};

server_exchange_state::server_exchange_state(std::shared_ptr<stream> stream,
	request_head request) :
	m_impl(std::make_shared<impl>(std::move(stream), std::move(request)))
{
}

server_exchange_state::~server_exchange_state() = default;

protocol server_exchange_state::selected_protocol() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->selected_protocol() : protocol::v1_1;
}

std::string server_exchange_state::remote_address() const {
	if( not m_impl->m_stream ) return {};
	error_code error;
	auto endpoint = m_impl->m_stream->remote_endpoint(error);
	return error ? std::string{} : endpoint.address().to_string();
}

uint16_t server_exchange_state::remote_port() const noexcept {
	if( not m_impl->m_stream ) return 0;
	error_code error;
	auto endpoint = m_impl->m_stream->remote_endpoint(error);
	return error ? 0 : endpoint.port();
}

asio::ip::tcp::endpoint server_exchange_state::remote_endpoint(
	error_code &error) const noexcept
{
	if( not m_impl->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->m_stream->remote_endpoint(error);
}

asio::ip::tcp::endpoint server_exchange_state::local_endpoint(
	error_code &error) const noexcept
{
	if( not m_impl->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->m_stream->local_endpoint(error);
}

stream_capabilities server_exchange_state::capabilities() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->capabilities() : stream_capabilities{};
}

const request_head &server_exchange_state::request() const noexcept {
	return m_impl->m_request;
}

std::shared_ptr<body_reader_state>
server_exchange_state::request_body() const noexcept {
	return m_impl->m_request_body;
}

std::shared_ptr<body_writer_state>
server_exchange_state::response_body() const noexcept {
	return m_impl->m_response_body;
}

void server_exchange_state::respond(response_head head, body_spec spec,
	error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_response_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return;
	}
	if( state->m_response_started or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	state->m_response_pending = true;
	state->m_response_body->configure(spec);
	state->m_stream->respond_sync(std::move(head), spec, error);
	state->m_response_pending = false;
	if( not error )
	{
		state->m_response_started = true;
		state->m_response_body->set_ready();
	}
}

void server_exchange_state::respond(response_head head, body_spec spec,
	operation_handler_t handler)
{
	auto state = m_impl;
	if( state->m_response_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress));
		return;
	}
	if( state->m_response_started or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	state->m_response_pending = true;
	state->m_response_body->configure(spec);
	state->m_stream->respond(std::move(head), spec,
	[state, handler = std::move(handler)]
	(error_code error) mutable
	{
		state->m_response_pending = false;
		if( not error )
		{
			state->m_response_started = true;
			state->m_response_body->set_ready();
		}
		std::move(handler)(error);
	});
}

void server_exchange_state::inform(response_head head,
	error_code &error) noexcept
{
	if( head.status.code() < 100 or head.status.code() >= 200 or
		head.status.code() == status::switching_protocols )
	{
		error = make_error_code(errc::invalid_status);
		return;
	}
	if( not m_impl->m_stream or m_impl->m_response_started )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	m_impl->m_stream->inform_sync(std::move(head), error);
}

void server_exchange_state::inform(response_head head,
	operation_handler_t handler)
{
	if( head.status.code() < 100 or head.status.code() >= 200 or
		head.status.code() == status::switching_protocols )
	{
		std::move(handler)(make_error_code(errc::invalid_status));
		return;
	}
	if( not m_impl->m_stream or m_impl->m_response_started )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	m_impl->m_stream->inform(std::move(head), std::move(handler));
}

std::shared_ptr<duplex_state> server_exchange_state::accept_duplex(
	response_head head, error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_response_pending or state->m_response_started or
		state->m_duplex_taken or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	state->m_response_pending = true;
	state->m_response_body->configure(body_spec::streaming());
	auto result = state->m_stream->open_duplex_sync(
		optional<response_head>(std::move(head)), error);
	state->m_response_pending = false;
	if( not error )
	{
		state->m_response_started = true;
		state->m_duplex_taken = true;
		state->m_response_body->set_ready();
	}
	return result;
}

void server_exchange_state::accept_duplex(response_head head,
	duplex_handler_t handler)
{
	auto state = m_impl;
	if( state->m_response_pending or state->m_response_started or
		state->m_duplex_taken or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			std::shared_ptr<duplex_state>{});
		return;
	}
	state->m_response_pending = true;
	state->m_response_body->configure(body_spec::streaming());
	state->m_stream->open_duplex(optional<response_head>(std::move(head)),
	[state, handler = std::move(handler)]
	(error_code error, std::shared_ptr<duplex_state> channel) mutable {
		state->m_response_pending = false;
		if( not error )
		{
			state->m_response_started = true;
			state->m_duplex_taken = true;
			state->m_response_body->set_ready();
		}
		std::move(handler)(error, std::move(channel));
	});
}

void server_exchange_state::cancel() noexcept
{
	if( m_impl->m_duplex_taken ) return;
	if( m_impl->m_stream and m_impl->m_stream->reusable() ) return;
	m_impl->m_request_body->cancel();
	m_impl->m_response_body->cancel();
	if( m_impl->m_stream )
		m_impl->m_stream->cancel();
}

std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream, request_head request)
{
	return std::make_shared<server_exchange_state>(
		std::move(stream), std::move(request));
}

} //namespace riwo::http_exp::detail
