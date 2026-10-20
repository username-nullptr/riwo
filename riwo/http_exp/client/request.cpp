// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request.h"

namespace riwo::http_exp
{

request::request(http_exp::method request_method, riwo::url request_url) :
	m_impl(std::make_unique<impl>(
		std::move(request_method), std::move(request_url)))
{
}

request::~request() = default;
request::request(request &&) noexcept = default;
request &request::operator=(request &&) noexcept = default;

http_exp::method request::method() const noexcept {
	return m_impl->head.method;
}

const riwo::url &request::url() const noexcept {
	return m_impl->head.url;
}

http_exp::fields &request::headers() noexcept {
	return m_impl->head.headers;
}

const http_exp::fields &request::headers() const noexcept {
	return m_impl->head.headers;
}

request &request::connect_protocol(std::string protocol)
{
	if( method().known() != http_exp::method::connect or protocol.empty() )
		throw std::invalid_argument(
			"Extended CONNECT requires CONNECT and a non-empty protocol");
	m_impl->head.connect_protocol = std::move(protocol);
	return *this;
}

const optional<std::string> &request::connect_protocol() const noexcept {
	return m_impl->head.connect_protocol;
}

request &request::upgrade_protocol(std::string protocol)
{
	if( protocol.empty() )
		throw std::invalid_argument("Upgrade protocol cannot be empty");
	m_impl->head.upgrade_protocol = std::move(protocol);
	return *this;
}

const optional<std::string> &request::upgrade_protocol() const noexcept {
	return m_impl->head.upgrade_protocol;
}

request &request::body(const const_buffer &bytes)
{
	m_impl->body = bytes;
	m_impl->source.reset();
	m_impl->file.clear();
	m_impl->body_spec = http_exp::body_spec::known(bytes.size());
	m_impl->trailers = {};
	return *this;
}

request &request::borrowed_body(const_buffer body)
{
	return this->body(body);
}

request &request::file(std::filesystem::path path)
{
	m_impl->body.reset();
	m_impl->source.reset();
	m_impl->file = std::move(path);
	m_impl->body_spec = http_exp::body_spec::streaming();
	m_impl->trailers = {};
	return *this;
}

request &request::trailers(fields values)
{
	body_spec::trailer_spec description;
	for( const auto &value : values )
	{
		const auto duplicate = std::ranges::any_of(description.names,
		[&](const std::string &name) {
			return name.size() == value.name.size() and
				std::ranges::equal(name, value.name, [](char lhs, char rhs) {
					return std::tolower(static_cast<unsigned char>(lhs)) ==
						std::tolower(static_cast<unsigned char>(rhs));
				});
		});
		if( not duplicate ) description.names.push_back(value.name);
	}
	if( auto length = m_impl->body_spec.length() )
		m_impl->body_spec = body_spec::known(*length, std::move(description));
	else if( m_impl->body_spec.type() == body_spec::mode::streaming )
		m_impl->body_spec = body_spec::streaming(std::move(description));
	else if( not values.empty() )
		throw std::invalid_argument("An empty HTTP body cannot have trailers");
	m_impl->trailers = std::move(values);
	return *this;
}

const body_spec &request::body_description() const noexcept {
	return m_impl->body_spec;
}

bool request::has_body_source() const noexcept {
	return m_impl->body.has_value() or static_cast<bool>(m_impl->source) or
		not m_impl->file.empty();
}

} //namespace riwo::http_exp
