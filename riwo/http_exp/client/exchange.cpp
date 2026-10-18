// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"
#include <riwo/http_exp/protocol/detail/session.h>

namespace riwo::http_exp::detail
{

class client_exchange_state::impl
{
public:
	explicit impl(std::shared_ptr<stream> value) : stream_(std::move(value)),
		request_body_(stream_ ? stream_->outbound_body() : make_body_writer_state()),
		response_body_(stream_ ? stream_->inbound_body() : make_body_reader_state())
	{
		if( !request_body_ )
			request_body_ = make_body_writer_state();
		if( !response_body_ )
			response_body_ = make_body_reader_state();
	}

	std::shared_ptr<stream> stream_;
	std::shared_ptr<body_writer_state> request_body_;
	std::shared_ptr<body_reader_state> response_body_;
	bool wait_pending = false;
	bool response_ready = false;
	response_head response {};
};

client_exchange_state::client_exchange_state(std::shared_ptr<stream> stream) :
	impl_(std::make_shared<impl>(std::move(stream)))
{
}

client_exchange_state::~client_exchange_state() = default;

protocol client_exchange_state::selected_protocol() const noexcept {
	return impl_->stream_ ? impl_->stream_->selected_protocol() : protocol::v1_1;
}

stream_capabilities client_exchange_state::capabilities() const noexcept {
	return impl_->stream_ ? impl_->stream_->capabilities() : stream_capabilities{};
}

std::shared_ptr<body_writer_state>
client_exchange_state::request_body() const noexcept {
	return impl_->request_body_;
}

std::shared_ptr<body_reader_state>
client_exchange_state::response_body() const noexcept {
	return impl_->response_body_;
}

response_head client_exchange_state::wait_response(error_code &error) noexcept
{
	auto state = impl_;
	if( state->wait_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return {};
	}
	if( state->response_ready )
	{
		error.clear();
		return state->response;
	}
	if( !state->stream_ )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	state->wait_pending = true;
	auto result = state->stream_->wait_response_sync(error);
	state->wait_pending = false;
	if( !error )
	{
		state->response = result;
		state->response_ready = true;
		state->response_body_->set_ready();
	}
	return result;
}

void client_exchange_state::wait_response(response_handler handler)
{
	auto state = impl_;
	if( state->wait_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress),
			response_head{});
		return;
	}
	if( state->response_ready )
	{
		std::move(handler)(error_code{}, state->response);
		return;
	}
	if( !state->stream_ )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			response_head{});
		return;
	}
	state->wait_pending = true;
	state->stream_->wait_response(
	[state = std::move(state), handler = std::move(handler)]
	(error_code error, response_head result) mutable
	{
		state->wait_pending = false;
		if( !error )
		{
			state->response = result;
			state->response_ready = true;
			state->response_body_->set_ready();
		}
		std::move(handler)(error, std::move(result));
	});
}

void client_exchange_state::cancel() noexcept
{
	impl_->request_body_->cancel();
	impl_->response_body_->cancel();
	if( impl_->stream_ )
		impl_->stream_->cancel();
}

std::shared_ptr<client_exchange_state> make_client_exchange_state(
	std::shared_ptr<stream> stream)
{
	return std::make_shared<client_exchange_state>(std::move(stream));
}

} //namespace riwo::http_exp::detail
