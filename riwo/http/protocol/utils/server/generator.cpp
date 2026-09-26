// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "generator.h"
#include <riwo/http/protocol/utils/core/generator.h>
#include <riwo/http/protocol/utils/core/conditional.h>

namespace riwo::http { namespace
{

[[nodiscard]] bool safe_header(std::string_view name, std::string_view value) noexcept
{
	constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
	return not name.empty() and
		std::ranges::all_of(name, [&](unsigned char ch) {
			return std::isalnum(ch) or punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
		}) and
		value.find_first_of("\r\n") == std::string_view::npos and
		value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool safe_cookie_value(std::string_view value) noexcept
{
	return value.find_first_of("\r\n;") == std::string_view::npos and
		   value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool truthy(const value &attribute) noexcept
{
	auto result = attribute.to_bool();
	return result and *result;
}

[[nodiscard]] std::string cookie_attribute_data
(std::string_view name, const value &attribute)
{
	if( strtls::to_lower(name) == "secure" or
		strtls::to_lower(name) == "httponly" )
		return truthy(attribute) ? std::string(name) : std::string();

	if( strtls::to_lower(name) == "expires" )
	{
		auto seconds = attribute.get<uint64_t>();
		if( seconds )
		{
			return std::string(name) + "=" + format_http_date (
				std::chrono::system_clock::time_point(std::chrono::seconds(*seconds))
			);
		}
	}
	return std::string(name) + "=" + attribute.to_string();
}

[[nodiscard]] std::string cookies_data(cookies &values)
{
	std::string result {};
	for(auto &[name,item] : values)
	{
		auto cookie_value = item.value().to_string();
		if( not safe_header(name, cookie_value) or not safe_cookie_value(cookie_value) )
			continue;

		result += std::format("Set-Cookie: {}={}", name, cookie_value);
		for(auto &[attribute_name,attribute] : item.attributes())
		{
			auto serialized = cookie_attribute_data(attribute_name, attribute);
			if( not serialized.empty() and safe_header(attribute_name, serialized) and
				safe_cookie_value(serialized) )
				result += "; " + serialized;
		}
		result += "\r\n";
	}
	return result;
}

} //namespace

class RIWO_DECL_HIDDEN generator<protocol_model::server>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)
	using generator_ptr = std::shared_ptr<base_generator>;

public:
	explicit impl(version_enum version)
	{
		version_t::check(version);
		if( version == version::v10 )
			m_generator = std::make_shared<base_generator_v10>();
		else if( version == version::v11 )
			m_generator = std::make_shared<base_generator_v11>();
		// else ... ...
	}

public:
	generator_ptr m_generator {};
	status_enum m_status = status::ok;
	cookies_t m_cookies {};
};

generator<protocol_model::server>::generator(version_enum version) :
	mutable_headers(nullptr),
	mutable_cookies(nullptr),
	mutable_chunk_attributes(nullptr),
	m_impl(new impl(version))
{
	m_headers = &m_impl->m_generator->headers();
	m_cookies = &m_impl->m_cookies;
	m_chunk_attributes = &m_impl->m_generator->chunk_attributes();
}

generator<protocol_model::server>::~generator()
{
	delete m_impl;
}

generator<protocol_model::server>::generator(generator &&other) noexcept :
	mutable_headers(other.m_headers),
	mutable_cookies(other.m_cookies),
	mutable_chunk_attributes(other.m_chunk_attributes),
	m_impl(other.m_impl)
{
	other.m_impl = new impl(version());
	other.m_headers = &other.m_impl->m_generator->headers();
	other.m_cookies = &other.m_impl->m_cookies;
	other.m_chunk_attributes = &other.m_impl->m_generator->chunk_attributes();
}

generator<protocol_model::server> &generator<protocol_model::server>::operator=(generator &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = other.m_impl;
	m_headers = other.m_headers;
	m_cookies = other.m_cookies;
	m_chunk_attributes = other.m_chunk_attributes;

	other.m_impl = new impl(version());
	other.m_headers = &other.m_impl->m_generator->headers();
	other.m_cookies = &other.m_impl->m_cookies;
	other.m_chunk_attributes = &other.m_impl->m_generator->chunk_attributes();
	return *this;
}

generator<protocol_model::server> &generator<protocol_model::server>::set_status(status_enum status)
{
	status::check(status);
	m_impl->m_status = status;
	return *this;
}

status_enum generator<protocol_model::server>::status() const noexcept
{
	return m_impl->m_status;
}

std::string generator<protocol_model::server>::header_data(size_t body_size)
{
	return header_data(body_size, method::get);
}

std::string generator<protocol_model::server>::header_data
(size_t body_size, method_enum request_method)
{
	if( m_impl->m_generator->state() != generator_state::header )
		return {};

	std::string buf {};
	buf.reserve(4096);

	if( m_impl->m_status == status::none )
		m_impl->m_status = status::ok;

	buf = std::format("HTTP/{} {} {}\r\n",
		version::string(version()), m_impl->m_status,
		status::description(m_impl->m_status)
	);
	auto code = static_cast<uint16_t>(m_impl->m_status);
	if( code >= 100 and code < 200 and m_impl->m_status != status::switching_protocols )
	{
		if( m_impl->m_status != status::continue_upload )
		{
			for(auto &[key,item] : m_impl->m_generator->headers())
			{
				auto lower = strtls::to_lower(key);
				auto value = item.to_string();

				if( lower != "content-length" and lower != "transfer-encoding" and
					lower != "content-type" and lower != "set-cookie" and
					safe_header(key, value) )
					buf += std::format("{}: {}\r\n", key, value);
			}
		}
		m_impl->m_status = status::ok;
		return buf + "\r\n";
	}
	m_impl->m_generator->unset_header("set-cookie");
	const bool successful_connect = request_method == method::connect and
		code >= 200 and code < 300;

	const bool head_response = request_method == method::head;
	const bool no_body = head_response or successful_connect or
		m_impl->m_status == status::switching_protocols or
		m_impl->m_status == status::no_content or
		m_impl->m_status == status::reset_content or
		m_impl->m_status == status::not_modified;

	if( no_body )
	{
		if( head_response )
			m_impl->m_generator->set_header(header::content_length, body_size);

		else if( m_impl->m_status == status::reset_content )
			m_impl->m_generator->set_header(header::content_length, 0);

		else if( m_impl->m_status != status::not_modified )
			m_impl->m_generator->unset_header(header::content_length);

		buf += m_impl->m_generator->header_data_no_body (
			head_response or m_impl->m_status == status::not_modified
		);
	}
	else
		buf += m_impl->m_generator->header_data(body_size);

	buf += cookies_data(m_impl->m_cookies);
	return buf + "\r\n";
}

std::string generator<protocol_model::server>::body_data(const const_buffer &buffer)
{
	return m_impl->m_generator->body_data(buffer);
}

const_buffer generator<protocol_model::server>::body_buffer(const const_buffer &buffer) noexcept
{
	return m_impl->m_generator->body_buffer(buffer);
}

std::string generator<protocol_model::server>::chunk_end_data(const headers_t &trailer_headers)
{
	return m_impl->m_generator->chunk_end_data(trailer_headers);
}

version_enum generator<protocol_model::server>::version() const noexcept
{
	return m_impl->m_generator->version();
}

generator_state generator<protocol_model::server>::pro_state() const noexcept
{
	return m_impl->m_generator->state();
}

generator<protocol_model::server> &generator<protocol_model::server>::reset() noexcept
{
	m_impl->m_status = status::ok;
	m_impl->m_cookies.clear();
	m_impl->m_generator->reset();
	return *this;
}

} //namespace riwo::http
