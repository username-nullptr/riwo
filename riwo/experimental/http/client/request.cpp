// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request.h"
#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

request::request(method_t method, url_t url) :
	m_impl(std::make_unique<impl>(std::move(method), std::move(url)))
{

}

request::~request() = default;

request::request(request &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(method_t::get, url_t{});
}

request &request::operator=(request &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(method_t::get, url_t{});
	return *this;
}

method request::method() const noexcept
{
	return m_impl->m_head.method;
}

const url &request::url() const noexcept
{
	return m_impl->m_head.url;
}

fields &request::headers() noexcept
{
	return m_impl->m_head.headers;
}

const fields &request::headers() const noexcept
{
	return m_impl->m_head.headers;
}

request &request::set_connect_protocol_impl(std::string protocol)
{
	if( method().known() != method_t::connect or protocol.empty() )
	{
		invalid_argument::loc_throw (
			"Extended CONNECT requires CONNECT and a non-empty protocol"
		);
	}
	m_impl->m_head.connect_protocol = std::move(protocol);
	return *this;
}

optional<std::string> request::connect_protocol() const
{
	return m_impl->m_head.connect_protocol;
}

request &request::set_upgrade_protocol_impl(std::string protocol)
{
	if( protocol.empty() )
		invalid_argument::loc_throw("Upgrade protocol cannot be empty");

	m_impl->m_head.upgrade_protocol = std::move(protocol);
	return *this;
}

optional<std::string> request::upgrade_protocol() const
{
	return m_impl->m_head.upgrade_protocol;
}

request &request::set_body(const const_buffer &bytes)
{
	m_impl->m_body = bytes;
	m_impl->m_source.reset();
	m_impl->m_file.clear();

	m_impl->m_body_spec = body_spec::known(bytes.size());
	m_impl->m_trailers = {};
	return *this;
}

request &request::set_borrowed_body(const const_buffer &body)
{
	return set_body(body);
}

request &request::set_file(std::filesystem::path path)
{
	m_impl->m_body.reset();
	m_impl->m_source.reset();
	m_impl->m_file = std::move(path);

	m_impl->m_body_spec = body_spec::streaming();
	m_impl->m_trailers = {};
	return *this;
}

request &request::set_trailers(fields values)
{
	body_spec::trailer_spec description {};
	for(const auto &[key, value] : values)
	{
		const auto duplicate = std::ranges::any_of(description.names,
			[&](const std::string &name) {
				return strtls::ascii_equal(name, key, false);
			});
		if( not duplicate )
			description.names.emplace_back(key);
	}
	if( auto length = m_impl->m_body_spec.length() )
		m_impl->m_body_spec = body_spec::known(*length, std::move(description));

	else if( m_impl->m_body_spec.type() == body_spec::mode::streaming )
		m_impl->m_body_spec = body_spec::streaming(std::move(description));

	else if( not values.empty() )
		invalid_argument::loc_throw("An empty HTTP body cannot have trailers");

	m_impl->m_trailers = std::move(values);
	return *this;
}

request &request::set_options(request_options value)
{
	m_impl->m_options = std::move(value);
	return *this;
}

request &request::set_proxy(proxy_policy value)
{
	m_impl->m_options.proxy = std::move(value);
	return *this;
}

request &request::set_redirects(redirect_policy value)
{
	m_impl->m_options.redirects = value;
	return *this;
}

request &request::set_retries(retry_policy value)
{
	m_impl->m_options.retries = value;
	return *this;
}

request &request::set_auto_decompression(bool value)
{
	m_impl->m_options.auto_decompression = value;
	return *this;
}

request &request::on_upload_progress(progress_observer observer)
{
	m_impl->m_options.upload_progress = std::move(observer);
	return *this;
}

request &request::on_download_progress(progress_observer observer)
{
	m_impl->m_options.download_progress = std::move(observer);
	return *this;
}

const request_options &request::options() const noexcept
{
	return m_impl->m_options;
}

body_spec request::body_description() const
{
	return m_impl->m_body_spec;
}

bool request::has_body_source() const noexcept
{
	return m_impl->m_body.has_value() or
		   static_cast<bool>(m_impl->m_source) or
		   not m_impl->m_file.empty();
}

} //namespace riwo::experimental::http
