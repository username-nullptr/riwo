// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/client/exchange.h>
#include <riwo/http_exp/protocol/detail/session.h>

namespace riwo::http_exp::detail
{

class client_exchange_state::impl
{
public:
	explicit impl(std::shared_ptr<stream> value,
		std::shared_ptr<cookie_jar> cookie_store, riwo::url request_origin) :
		m_stream(std::move(value)), m_cookie_store(std::move(cookie_store)),
		m_origin(std::move(request_origin)),
		m_request_body(m_stream ? m_stream->outbound_body() : make_body_writer_state()),
		m_reply_body(m_stream ? m_stream->inbound_body() : make_body_reader_state())
	{
		if( not m_request_body )
			m_request_body = make_body_writer_state();
		if( not m_reply_body )
			m_reply_body = make_body_reader_state();
	}

	std::shared_ptr<stream> m_stream;
	std::shared_ptr<cookie_jar> m_cookie_store;
	riwo::url m_origin;
	std::shared_ptr<body_writer_state> m_request_body;
	std::shared_ptr<body_reader_state> m_reply_body;
	bool m_wait_pending = false;
	bool m_reply_ready = false;
	bool m_duplex_taken = false;
	reply_head m_reply_head {};

	void capture_cookies(const reply_head &head) noexcept
	{
		if( not m_cookie_store )
			return ;
		try {
			m_cookie_store->store(m_origin, head.headers);
		}
		catch(...) {
		}
	}
};

client_exchange_state::client_exchange_state(std::shared_ptr<stream> stream,
	std::shared_ptr<cookie_jar> cookies, riwo::url origin) :
	m_impl(std::make_shared<impl>(std::move(stream), std::move(cookies),
		std::move(origin)))
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
client_exchange_state::reply_body() const noexcept {
	return m_impl->m_reply_body;
}

void *client_exchange_state::protocol_extension(
	const std::type_info &type) noexcept
{
	return m_impl->m_stream ?
		m_impl->m_stream->protocol_extension(type) : nullptr;
}

reply_head client_exchange_state::wait_reply(error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_wait_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return {};
	}
	if( state->m_reply_ready )
	{
		error.clear();
		return state->m_reply_head;
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
		state->capture_cookies(result);
		state->m_reply_head = result;
		state->m_reply_ready = true;
		state->m_reply_body->set_ready();
	}
	return result;
}

void client_exchange_state::wait_reply(reply_handler_t handler)
{
	auto state = m_impl;
	if( state->m_wait_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress),
			reply_head{});
		return ;
	}
	if( state->m_reply_ready )
	{
		std::move(handler)(error_code{}, state->m_reply_head);
		return ;
	}
	if( not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			reply_head{});
		return ;
	}
	state->m_wait_pending = true;
	state->m_stream->wait_response(
	[state, handler = std::move(handler)]
	(error_code error, reply_head result) mutable
	{
		state->m_wait_pending = false;
		if( not error )
		{
			state->capture_cookies(result);
			state->m_reply_head = result;
			state->m_reply_ready = true;
			state->m_reply_body->set_ready();
		}
		std::move(handler)(error, std::move(result));
	});
}

std::shared_ptr<duplex_state> client_exchange_state::release_duplex(
	error_code &error) noexcept
{
	auto state = m_impl;
	if( not state->m_reply_ready or state->m_duplex_taken or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	auto result = state->m_stream->open_duplex_sync({}, error);
	if( not error )
		state->m_duplex_taken = true;
	return result;
}

void client_exchange_state::release_duplex(duplex_handler_t handler)
{
	auto state = m_impl;
	if( not state->m_reply_ready or state->m_duplex_taken or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			std::shared_ptr<duplex_state>{});
		return ;
	}
	state->m_stream->open_duplex({},
	[state, handler = std::move(handler)]
	(error_code error, std::shared_ptr<duplex_state> channel) mutable {
		if( not error )
			state->m_duplex_taken = true;
		std::move(handler)(error, std::move(channel));
	});
}

void client_exchange_state::cancel() noexcept
{
	if( m_impl->m_duplex_taken )
		return ;
	m_impl->m_request_body->cancel();
	m_impl->m_reply_body->cancel();
	if( m_impl->m_stream )
		m_impl->m_stream->cancel();
}

std::shared_ptr<client_exchange_state> make_client_exchange_state(
	std::shared_ptr<stream> stream, std::shared_ptr<cookie_jar> cookies,
	riwo::url origin)
{
	return std::make_shared<client_exchange_state>(std::move(stream),
		std::move(cookies), std::move(origin));
}

} //namespace riwo::http_exp::detail
