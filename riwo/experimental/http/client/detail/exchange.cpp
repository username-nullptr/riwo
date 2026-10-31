// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/http/session/session_stream.h>
#include <riwo/experimental/http/client/exchange.h>

namespace riwo::experimental::http::detail
{

class RIWO_DECL_HIDDEN client_exchange_state::impl
{
public:
	explicit impl(session_stream_ptr value, cookie_jar_ptr cookie_store, url request_origin) :
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

	void capture_cookies(const reply_head &head) noexcept
	{
		if( not m_cookie_store )
			return ;
		try {
			m_cookie_store->store(m_origin, head.headers);
		}
		catch(...) {}
	}

public:
	session_stream_ptr m_stream {};
	cookie_jar_ptr m_cookie_store {};

	url m_origin {};

	body_writer_state_ptr m_request_body {};
	body_reader_state_ptr m_reply_body {};

	bool m_wait_pending = false;
	bool m_reply_ready = false;
	bool m_duplex_taken = false;

	reply_head m_reply_head {};
};

client_exchange_state::client_exchange_state(session_stream_ptr stream, cookie_jar_ptr cookies, url origin) :
	m_impl(std::make_shared<impl>(std::move(stream), std::move(cookies), std::move(origin)))
{

}

client_exchange_state::~client_exchange_state() = default;

protocol client_exchange_state::selected_protocol() const noexcept
{
	return m_impl->m_stream ? m_impl->m_stream->selected_protocol() : protocol::v1_1;
}

stream_capabilities client_exchange_state::capabilities() const noexcept
{
	return m_impl->m_stream ? m_impl->m_stream->capabilities() : stream_capabilities{};
}

body_writer_state_ptr client_exchange_state::request_body() const noexcept
{
	return m_impl->m_request_body;
}

body_reader_state_ptr client_exchange_state::reply_body() const noexcept
{
	return m_impl->m_reply_body;
}

void *client_exchange_state::protocol_extension(const std::type_info &type) noexcept
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
		std::move(handler) (
			make_error_code(errc::operation_not_permitted),
			reply_head {}
		);
		return ;
	}
	state->m_wait_pending = true;

	state->m_stream->wait_response(
	[state, handler = std::move(handler)](error_code error, reply_head result) mutable
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

duplex_state_ptr client_exchange_state::release_duplex(error_code &error) noexcept
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
		std::move(handler) (
			make_error_code(errc::operation_not_permitted),
			duplex_state_ptr {}
		);
		return ;
	}
	state->m_stream->open_duplex({},
	[state, handler = std::move(handler)](error_code error, duplex_state_ptr channel) mutable
	{
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

client_exchange_state_ptr
make_client_exchange_state(session_stream_ptr stream, cookie_jar_ptr cookies, url origin)
{
	return std::make_shared<client_exchange_state>(
		std::move(stream), std::move(cookies), std::move(origin)
	);
}

} //namespace riwo::experimental::http::detail
