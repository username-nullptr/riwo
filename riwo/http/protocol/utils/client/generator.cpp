// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "generator.h"
#include <riwo/core/algorithm/misc.h>

namespace riwo::http
{

class RIWO_DECL_HIDDEN generator<protocol_model::client>::impl
{
	RIWO_DISABLE_COPY(impl)
	using generator_ptr = std::shared_ptr<base_generator>;

public:
	impl(impl&&) noexcept = default;
	impl &operator=(impl&&) noexcept = default;

public:
	explicit impl(version_enum version, url_t url, request_arg_t request,
		request_target_form target_form) :
		m_url(std::move(url)), m_target_form(target_form)
	{
		version_t::check(version);
		if( version == version::v10 )
			m_generator = std::make_shared<base_generator_v10>();
		else if( version == version::v11 )
			m_generator = std::make_shared<base_generator_v11>();
		// else ... ...

		set_request_arg(std::move(request));
	}

	void set_request_arg(request_arg_t request) noexcept
	{
		for(auto &[key,value] : request.headers())
			m_generator->set_header(key, std::move(value));

		for(auto &[key,value] : request.cookies())
			m_cookies[key] = std::move(value);

		for(auto &value : request.chunk_attributes())
			m_chunk_attributes.emplace(value);
	}

public:
	url_t m_url {};
	generator_ptr m_generator;

	cookies_t m_cookies {};
	values_t m_chunk_attributes {};

	request_target_form m_target_form =
		request_target_form::origin;
};

generator<protocol_model::client>::generator(version_enum version, url_t url,
	request_arg_t arg, request_target_form target_form) :
	mutable_headers(nullptr),
	mutable_cookies(nullptr),
	mutable_chunk_attributes(nullptr),
	m_impl(new impl(version, std::move(url), std::move(arg), target_form))
{
	m_headers = &m_impl->m_generator->headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_chunk_attributes;
}

generator<protocol_model::client>::generator(url_t url, request_arg_t arg,
	request_target_form target_form) :
	generator(version_enum::v11, std::move(url), std::move(arg), target_form)
{

}

generator<protocol_model::client>::~generator()
{
	delete m_impl;
}

generator<protocol_model::client>::generator(generator &&other) noexcept :
	mutable_headers(nullptr),
	mutable_cookies(nullptr),
	mutable_chunk_attributes(nullptr),
	m_impl(new impl(std::move(*other.m_impl)))
{
	m_headers = &m_impl->m_generator->headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_chunk_attributes;
}

generator<protocol_model::client> &generator<protocol_model::client>::operator=(generator &&other) noexcept
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

generator<protocol_model::client> &generator<protocol_model::client>::emplace(url_t url, request_arg_t arg)
{
	m_impl->m_url = std::move(url);
	m_impl->set_request_arg(std::move(arg));
	return *this;
}

generator<protocol_model::client> &generator<protocol_model::client>::emplace(request_arg arg)
{
	m_impl->set_request_arg(std::move(arg));
	return *this;
}

generator<protocol_model::client> &generator<protocol_model::client>::emplace(url_t url)
{
	m_impl->m_url = std::move(url);
	return *this;
}

const url &generator<protocol_model::client>::url() const noexcept
{
	return m_impl->m_url;
}

url &generator<protocol_model::client>::url() noexcept
{
	return m_impl->m_url;
}

request_arg generator<protocol_model::client>::arg() const noexcept
{
	request_arg_t arg;
	auto *self = remove_const(this);

	for(auto &[key,value] : self->headers())
		arg.set_header(key, std::move(value));

	for(auto &[key,value] : self->cookies())
		arg.set_cookie(key, std::move(value));

	for(auto &value : self->chunk_attributes())
		arg.set_chunk_attribute(value);
	return arg;
}

generator<protocol_model::client>::operator request_arg_t() const noexcept
{
	return arg();
}

generator<protocol_model::client>&
generator<protocol_model::client>::set_target_form(request_target_form form) noexcept
{
	m_impl->m_target_form = form;
	return *this;
}

request_target_form generator<protocol_model::client>::target_form() const noexcept
{
	return m_impl->m_target_form;
}

std::string generator<protocol_model::client>::header_data(method_enum method, size_t body_size)
{
	if( m_impl->m_generator->state() != generator_state::header )
		return {};

	auto &url = this->url();
	auto buf = std::string(method::string(method)) + " ";
	{
		auto path = std::string(url.encoded_path());
		if( url.has_query() )
			path += '?' + url.encoded_query();

		std::string target {};
		if( method == method::connect or m_impl->m_target_form == request_target_form::authority )
		{
			target = std::string(url.host());
			if( target.find(':') != std::string::npos and not target.starts_with('[') )
				target = '[' + target + ']';
			target += ':' + std::to_string(url.port());
		}
		else if( m_impl->m_target_form == request_target_form::absolute )
		{
			target = url.to_string();
			if( url.has_fragment() )
				target.erase(target.rfind('#'));
		}
		else if( m_impl->m_target_form == request_target_form::asterisk )
			target = "*";
		else
			target = std::move(path);

		buf += target + " HTTP/"
			+ version::string(m_impl->m_generator->version())
			+ "\r\n";
	}
	auto host = std::string(url.host());
	if( host.find(':') != std::string::npos and not host.starts_with('[') )
		host = '[' + host + ']';

	if( url.port() != 0 and
		((url.protocol() == "http" and url.port() != 80) or
		 (url.protocol() == "https" and url.port() != 443)) )
		host += ':' + std::to_string(url.port());

	mutable_headers::set_header(header::host, std::move(host));
	buf += m_impl->m_generator->header_data(body_size);

	if( not cookies().empty() )
	{
		buf += "Cookie: ";
		bool first = true;

		for(auto &[key,value] : cookies())
		{
			auto cookie_value = value.to_string();
			if( key.empty() or key.find_first_of("()<>@,;:\\\"/[]?={} \t\r\n") !=
				std::string::npos or cookie_value.find_first_of(";\r\n") != std::string::npos or
				cookie_value.find('\0') != std::string::npos )
				continue;

			if( not first )
				buf += "; ";

			first = false;
			buf += std::format("{}={}", key, cookie_value);
		}
		if( first )
			buf.erase(buf.size() - std::string("Cookie: ").size());
		else
			buf += "\r\n";
	}
	return buf + "\r\n";
}

std::string generator<protocol_model::client>::body_data(const const_buffer &buffer)
{
	for(auto &attribute : m_impl->m_chunk_attributes)
		m_impl->m_generator->set_chunk_attribute(attribute);
	m_impl->m_chunk_attributes.clear();
	return m_impl->m_generator->body_data(buffer);
}

std::string generator<protocol_model::client>::chunk_end_data(const headers_t &trailer_headers)
{
	return m_impl->m_generator->chunk_end_data(trailer_headers);
}

version_enum generator<protocol_model::client>::version() const noexcept
{
	return m_impl->m_generator->version();
}

generator_state generator<protocol_model::client>::pro_state() const noexcept
{
	return m_impl->m_generator->state();
}

generator<protocol_model::client> &generator<protocol_model::client>::reset() noexcept
{
	m_impl->m_cookies.clear();
	m_impl->m_chunk_attributes.clear();
	m_impl->m_generator->reset();
	return *this;
}

base_generator &generator<protocol_model::client>::base() noexcept
{
	return *m_impl->m_generator;
}

} //namespace riwo::http
