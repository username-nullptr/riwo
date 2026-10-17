// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"

namespace riwo::http_exp
{

server_exchange::server_exchange(executor_type executor) :
	executor_(std::move(executor)),
	request_body_(new body_reader(executor_)),
	response_body_(new body_writer(executor_))
{
}

server_exchange::~server_exchange() = default;

server_exchange::executor_type server_exchange::get_executor() const noexcept {
	return executor_;
}

protocol server_exchange::selected_protocol() const noexcept {
	return protocol_;
}

const server_request &server_exchange::request() const noexcept {
	return request_;
}

body_reader &server_exchange::request_body() noexcept {
	return *request_body_;
}

body_writer &server_exchange::response_body() noexcept {
	return *response_body_;
}

void server_exchange::cancel() noexcept {}

stream_capabilities server_exchange::capabilities() const noexcept {
	return {};
}

} //namespace riwo::http_exp
