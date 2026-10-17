// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "response.h"

namespace riwo::http_exp
{

response::response() : body_(new body_reader(executor_)) {}
response::~response() = default;
response::response(response &&) noexcept = default;
response &response::operator=(response &&) noexcept = default;

response::executor_type response::get_executor() const noexcept {
	return executor_;
}

protocol response::selected_protocol() const noexcept {
	return protocol_;
}

http_exp::status response::status() const noexcept {
	return head_.status;
}

const fields &response::headers() const noexcept {
	return head_.headers;
}

body_reader &response::body() noexcept {
	return *body_;
}

std::span<const std::byte> response::buffered_body() const noexcept {
	return buffered_body_;
}

void response::cancel() noexcept {}

} //namespace riwo::http_exp
