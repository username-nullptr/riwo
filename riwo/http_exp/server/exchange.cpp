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
		stream_(std::move(value)), request_(std::move(metadata)),
		request_body_(stream_ ? stream_->inbound_body() : make_body_reader_state()),
		response_body_(stream_ ? stream_->outbound_body() : make_body_writer_state())
	{
		if( !request_body_ )
			request_body_ = make_body_reader_state();
		if( !response_body_ )
			response_body_ = make_body_writer_state();
	}

	std::shared_ptr<stream> stream_;
	request_head request_;
	std::shared_ptr<body_reader_state> request_body_;
	std::shared_ptr<body_writer_state> response_body_;
	bool response_pending = false;
	bool response_started = false;
};

server_exchange_state::server_exchange_state(std::shared_ptr<stream> stream,
	request_head request) :
	m_impl(std::make_shared<impl>(std::move(stream), std::move(request)))
{
}

server_exchange_state::~server_exchange_state() = default;

protocol server_exchange_state::selected_protocol() const noexcept {
	return m_impl->stream_ ? m_impl->stream_->selected_protocol() : protocol::v1_1;
}

stream_capabilities server_exchange_state::capabilities() const noexcept {
	return m_impl->stream_ ? m_impl->stream_->capabilities() : stream_capabilities{};
}

const request_head &server_exchange_state::request() const noexcept {
	return m_impl->request_;
}

std::shared_ptr<body_reader_state>
server_exchange_state::request_body() const noexcept {
	return m_impl->request_body_;
}

std::shared_ptr<body_writer_state>
server_exchange_state::response_body() const noexcept {
	return m_impl->response_body_;
}

void server_exchange_state::respond(response_head head, body_spec spec,
	error_code &error) noexcept
{
	auto state = m_impl;
	if( state->response_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return;
	}
	if( state->response_started || !state->stream_ )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	state->response_pending = true;
	state->response_body_->configure(spec);
	state->stream_->respond_sync(std::move(head), spec, error);
	state->response_pending = false;
	if( !error )
	{
		state->response_started = true;
		state->response_body_->set_ready();
	}
}

void server_exchange_state::respond(response_head head, body_spec spec,
	operation_handler handler)
{
	auto state = m_impl;
	if( state->response_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress));
		return;
	}
	if( state->response_started || !state->stream_ )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	state->response_pending = true;
	state->response_body_->configure(spec);
	state->stream_->respond(std::move(head), spec,
	[state, handler = std::move(handler)]
	(error_code error) mutable
	{
		state->response_pending = false;
		if( !error )
		{
			state->response_started = true;
			state->response_body_->set_ready();
		}
		std::move(handler)(error);
	});
}

void server_exchange_state::inform(response_head head,
	error_code &error) noexcept
{
	if( head.status.code() < 100 || head.status.code() >= 200 ||
		head.status.code() == status::switching_protocols )
	{
		error = make_error_code(errc::invalid_status);
		return;
	}
	if( !m_impl->stream_ || m_impl->response_started )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	m_impl->stream_->inform_sync(std::move(head), error);
}

void server_exchange_state::inform(response_head head,
	operation_handler handler)
{
	if( head.status.code() < 100 || head.status.code() >= 200 ||
		head.status.code() == status::switching_protocols )
	{
		std::move(handler)(make_error_code(errc::invalid_status));
		return;
	}
	if( !m_impl->stream_ || m_impl->response_started )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	m_impl->stream_->inform(std::move(head), std::move(handler));
}

void server_exchange_state::cancel() noexcept
{
	if( m_impl->stream_ && m_impl->stream_->reusable() ) return;
	m_impl->request_body_->cancel();
	m_impl->response_body_->cancel();
	if( m_impl->stream_ )
		m_impl->stream_->cancel();
}

std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream, request_head request)
{
	return std::make_shared<server_exchange_state>(
		std::move(stream), std::move(request));
}

} //namespace riwo::http_exp::detail
