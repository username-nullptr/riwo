// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"

namespace riwo::http_exp
{

client_exchange::client_exchange() :
	request_body_(new body_writer(executor_)),
	response_body_(new body_reader(executor_))
{
}

client_exchange::~client_exchange() = default;
client_exchange::client_exchange(client_exchange &&) noexcept = default;
client_exchange &client_exchange::operator=(client_exchange &&) noexcept = default;

client_exchange::executor_type client_exchange::get_executor() const noexcept {
	return executor_;
}

protocol client_exchange::selected_protocol() const noexcept {
	return protocol_;
}

body_writer &client_exchange::request_body() noexcept {
	return *request_body_;
}

body_reader &client_exchange::response_body() noexcept {
	return *response_body_;
}

void client_exchange::cancel() noexcept {}

stream_capabilities client_exchange::capabilities() const noexcept {
	return {};
}

} //namespace riwo::http_exp
