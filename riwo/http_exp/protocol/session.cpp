// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/session.h"

namespace riwo::http_exp::detail
{

namespace
{
[[nodiscard]] error_code unsupported() noexcept {
	return make_error_code(errc::operation_not_supported);
}
} //namespace

stream_capabilities stream::capabilities() const noexcept {
	return {};
}

request_head stream::request() const {
	return {};
}

std::shared_ptr<body_reader_state> stream::inbound_body() const noexcept {
	return make_body_reader_state();
}

std::shared_ptr<body_writer_state> stream::outbound_body() const noexcept {
	return make_body_writer_state();
}

response_head stream::wait_response_sync(error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void stream::wait_response(response_handler handler) {
	std::move(handler)(unsupported(), response_head{});
}

auto stream::remote_endpoint(error_code &error) const noexcept -> endpoint_type
{
	error = unsupported();
	return {};
}

auto stream::local_endpoint(error_code &error) const noexcept -> endpoint_type
{
	error = unsupported();
	return {};
}

void stream::respond_sync(response_head, body_spec, error_code &error) noexcept {
	error = unsupported();
}

void stream::respond(response_head, body_spec, operation_handler handler) {
	std::move(handler)(unsupported());
}

void stream::inform_sync(response_head, error_code &error) noexcept {
	error = unsupported();
}

void stream::inform(response_head, operation_handler handler) {
	std::move(handler)(unsupported());
}

std::shared_ptr<stream> client_session::open_sync(request_head, body_spec,
	error_code &error) noexcept
{
	error = unsupported();
	return {};
}

} //namespace riwo::http_exp::detail
