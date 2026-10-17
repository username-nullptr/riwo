// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request.h"

namespace riwo::http_exp
{

request::request(http_exp::method request_method, riwo::url request_url) :
	method_(std::move(request_method)), url_(std::move(request_url))
{
}

request::~request() = default;
request::request(request &&) noexcept = default;
request &request::operator=(request &&) noexcept = default;

http_exp::method request::method() const noexcept {
	return method_;
}

const riwo::url &request::url() const noexcept {
	return url_;
}

http_exp::fields &request::headers() noexcept {
	return headers_;
}

const http_exp::fields &request::headers() const noexcept {
	return headers_;
}

request &request::borrowed_body(const_buffer body)
{
	owned_body_.clear();
	body_owner_.reset();
	file_.clear();
	borrowed_body_ = body;
	body_spec_ = http_exp::body_spec::known(body.size());
	trailers_ = {};
	return *this;
}

request &request::file(std::filesystem::path path)
{
	owned_body_.clear();
	body_owner_.reset();
	borrowed_body_ = const_buffer(nullptr, 0);
	file_ = std::move(path);
	body_spec_ = http_exp::body_spec::streaming();
	trailers_ = {};
	return *this;
}

request &request::trailers(fields values)
{
	trailers_ = std::move(values);
	return *this;
}

const body_spec &request::body_description() const noexcept {
	return body_spec_;
}

bool request::has_body_source() const noexcept {
	return body_spec_.type() != body_spec::mode::empty;
}

} //namespace riwo::http_exp
