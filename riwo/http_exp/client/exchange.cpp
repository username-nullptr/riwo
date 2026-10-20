// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"
#include <riwo/http_exp/protocol/detail/session.h>

namespace riwo::http_exp::detail
{

class client_exchange_state::impl
{
public:
	explicit impl(std::shared_ptr<stream> value) : m_stream(std::move(value)),
		m_request_body(m_stream ? m_stream->outbound_body() : make_body_writer_state()),
		m_response_body(m_stream ? m_stream->inbound_body() : make_body_reader_state())
	{
		if( not m_request_body )
			m_request_body = make_body_writer_state();
		if( not m_response_body )
			m_response_body = make_body_reader_state();
	}

	std::shared_ptr<stream> m_stream;
	std::shared_ptr<body_writer_state> m_request_body;
	std::shared_ptr<body_reader_state> m_response_body;
	bool m_wait_pending = false;
	bool m_response_ready = false;
	bool m_duplex_taken = false;
	response_head m_response {};
};

client_exchange_state::client_exchange_state(std::shared_ptr<stream> stream) :
	m_impl(std::make_shared<impl>(std::move(stream)))
{
}

client_exchange_state::~client_exchange_state() = default;

protocol client_exchange_state::selected_protocol() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->selected_protocol() : protocol::v1_1;
}

stream_capabilities client_exchange_state::capabilities() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->capabilities() : stream_capabilities{};
}

std::shared_ptr<body_writer_state>
client_exchange_state::request_body() const noexcept {
	return m_impl->m_request_body;
}

std::shared_ptr<body_reader_state>
client_exchange_state::response_body() const noexcept {
	return m_impl->m_response_body;
}

response_head client_exchange_state::wait_response(error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_wait_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return {};
	}
	if( state->m_response_ready )
	{
		error.clear();
		return state->m_response;
	}
	if( not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	state->m_wait_pending = true;
	auto result = state->m_stream->wait_response_sync(error);
	state->m_wait_pending = false;
	if( not error )
	{
		state->m_response = result;
		state->m_response_ready = true;
		state->m_response_body->set_ready();
	}
	return result;
}

void client_exchange_state::wait_response(response_handler handler)
{
	auto state = m_impl;
	if( state->m_wait_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress),
			response_head{});
		return;
	}
	if( state->m_response_ready )
	{
		std::move(handler)(error_code{}, state->m_response);
		return;
	}
	if( not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			response_head{});
		return;
	}
	state->m_wait_pending = true;
	state->m_stream->wait_response(
	[state, handler = std::move(handler)]
	(error_code error, response_head result) mutable
	{
		state->m_wait_pending = false;
		if( not error )
		{
			state->m_response = result;
			state->m_response_ready = true;
			state->m_response_body->set_ready();
		}
		std::move(handler)(error, std::move(result));
	});
}

std::shared_ptr<duplex_state> client_exchange_state::release_duplex(
	error_code &error) noexcept
{
	auto state = m_impl;
	if( not state->m_response_ready or state->m_duplex_taken or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	auto result = state->m_stream->open_duplex_sync({}, error);
	if( not error ) state->m_duplex_taken = true;
	return result;
}

void client_exchange_state::release_duplex(duplex_handler handler)
{
	auto state = m_impl;
	if( not state->m_response_ready or state->m_duplex_taken or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			std::shared_ptr<duplex_state>{});
		return;
	}
	state->m_stream->open_duplex({},
	[state, handler = std::move(handler)]
	(error_code error, std::shared_ptr<duplex_state> channel) mutable {
		if( not error ) state->m_duplex_taken = true;
		std::move(handler)(error, std::move(channel));
	});
}

void client_exchange_state::cancel() noexcept
{
	if( m_impl->m_duplex_taken ) return;
	m_impl->m_request_body->cancel();
	m_impl->m_response_body->cancel();
	if( m_impl->m_stream )
		m_impl->m_stream->cancel();
}

std::shared_ptr<client_exchange_state> make_client_exchange_state(
	std::shared_ptr<stream> stream)
{
	return std::make_shared<client_exchange_state>(std::move(stream));
}

} //namespace riwo::http_exp::detail
