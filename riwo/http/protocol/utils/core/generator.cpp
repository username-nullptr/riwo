// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "generator.h"

namespace riwo::http
{
namespace
{

[[nodiscard]] bool valid_field_name(std::string_view value) noexcept
{
	constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
	return not value.empty() and std::ranges::all_of(value, [&](unsigned char ch) {
		return std::isalnum(ch) or punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
	});
}

[[nodiscard]] bool valid_field_value(std::string_view value) noexcept
{
	return value.find('\r') == std::string_view::npos and
		   value.find('\n') == std::string_view::npos and
		   value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool token_char(unsigned char ch) noexcept
{
	constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
	return std::isalnum(ch) or punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
}

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool valid_chunk_extension(std::string_view value) noexcept
{
	value = trim_ows(value);
	size_t pos = 0;

	while( pos < value.size() and token_char(static_cast<unsigned char>(value[pos])) )
		++pos;

	if( pos == 0 )
		return false;

	while( pos < value.size() and (value[pos] == ' ' or value[pos] == '\t') )
		++pos;

	if( pos == value.size() )
		return true;

	if( value[pos++] != '=' )
		return false;

	while( pos < value.size() and (value[pos] == ' ' or value[pos] == '\t') )
		++pos;

	if( pos == value.size() )
		return false;

	if( value[pos] != '"' )
	{
		auto begin = pos;
		while( pos < value.size() and token_char(static_cast<unsigned char>(value[pos])) )
			++pos;
		return pos > begin and trim_ows(value.substr(pos)).empty();
	}
	++pos;
	bool closed = false;

	while( pos < value.size() )
	{
		auto ch = static_cast<unsigned char>(value[pos++]);
		if( ch == '"' )
		{
			closed = true;
			break;
		}
		if( ch == '\\' )
		{
			if( pos == value.size() )
				return false;
			ch = static_cast<unsigned char>(value[pos++]);
		}
		if( ch != '\t' and (ch < 0x20 or ch == 0x7F) )
			return false;
	}
	return closed and trim_ows(value.substr(pos)).empty();
}

[[nodiscard]] std::string serialize_headers(const headers &values)
{
	std::string result;
	for(auto &[key,value] : values)
	{
		auto text = value.to_string();
		if( valid_field_name(key) and valid_field_value(text) )
			result += std::format("{}: {}\r\n", key, text);
	}
	return result;
}

} //namespace

class RIWO_DECL_HIDDEN generator<protocol_model::base>::impl
{
	RIWO_DISABLE_COPY(impl)

public:
	impl() = default;

	headers_t m_headers {{
		header::content_type,
		"text/plain; charset=utf-8"
	}};
	values_t m_chunk_attributes {};
	size_t m_content_length = 0;

	state_t m_state {};
};

generator<protocol_model::base>::generator() :
	mutable_headers(nullptr),
	mutable_chunk_attributes(nullptr),
	m_impl(new impl())
{
	m_headers = &m_impl->m_headers;
	m_chunk_attributes = &m_impl->m_chunk_attributes;
}

generator<protocol_model::base>::~generator()
{
	delete m_impl;
}

generator<protocol_model::base> &generator<protocol_model::base>::reset()
{
	headers().clear();
	headers()[header::content_type] = "text/plain; charset=utf-8";
	chunk_attributes().clear();
	m_impl->m_content_length = 0;
	m_impl->m_state = state_t::header;
	return *this;
}

std::string generator<protocol_model::base>::header_data(size_t body_size) noexcept
{
	if( state() != state_t::header )
		return {};

	auto &headers = this->headers();
	auto it = headers.find(header::content_length);

	if( it == headers.end() )
	{
		m_impl->m_content_length = body_size;
		m_impl->m_state = state_t::content_length;
		m_impl->m_headers[header::content_length] = m_impl->m_content_length;
	}
	else
	{
		m_impl->m_content_length = *it->second.get<size_t>().or_else();
		m_impl->m_state = state_t::content_length;
	}
	return serialize_headers(headers);
}

std::string generator<protocol_model::base>::header_data_no_body
(bool preserve_content_length) noexcept
{
	if( state() != state_t::header )
		return {};

	auto &values = headers();
	values.erase(header::transfer_encoding);

	if( not preserve_content_length )
		values.erase(header::content_length);

	m_impl->m_content_length = 0;
	m_impl->m_state = state_t::finish;
	return serialize_headers(values);
}

std::string generator<protocol_model::base>::body_data(const const_buffer &buffer) noexcept
{
	if( m_impl->m_state == state_t::header or m_impl->m_state == state_t::finish )
		return {};

	else if( m_impl->m_state == state_t::content_length )
	{
		auto body = body_buffer(buffer);
		return {static_cast<const char*>(body.data()), body.size()};
	}
	std::string sum;
	sum += std::format("{:X}", buffer.size());

	for(auto &attr : m_impl->m_chunk_attributes)
	{
		if( auto value = trim_ows(attr.to_string()); valid_chunk_extension(value) )
			sum += "; " + std::string(value);
	}
	m_impl->m_chunk_attributes.clear();
	sum += "\r\n";

	return sum + std::string (
		static_cast<const char*>(buffer.data()), buffer.size()
	) + "\r\n";
}

const_buffer generator<protocol_model::base>::body_buffer
(const const_buffer &buffer) noexcept
{
	if( m_impl->m_state != state_t::content_length )
		return {};

	auto size = std::min(m_impl->m_content_length, buffer.size());
	m_impl->m_content_length -= size;

	if( m_impl->m_content_length == 0 )
		m_impl->m_state = state_t::finish;

	return {buffer.data(), size};
}

std::string generator<protocol_model::base>::chunk_end_data(const headers_t &trailer_headers) noexcept
{
	if( m_impl->m_state != state_t::chunk )
		return {};

	m_impl->m_state = state_t::finish;
	std::string buf = "0\r\n";

	return buf + serialize_headers(trailer_headers) + "\r\n";
}

std::string generator<protocol_model::base>::header_data() noexcept
{
	return header_data(0);
}

std::string generator<protocol_model::base>::chunk_end_data() noexcept
{
	return chunk_end_data({});
}

generator<protocol_model::base>::state_t generator<protocol_model::base>::state() const noexcept
{
	return m_impl->m_state;
}

version_enum generator_v10<protocol_model::base>::version() const noexcept
{
	return version_enum::v10;
}

std::string generator_v11<protocol_model::base>::header_data(size_t body_size) noexcept
{
	if( state() != state_t::header )
		return {};

	auto &headers = this->headers();
	if( auto it = headers.find(header::content_length); it == headers.end() )
	{
		it = headers.find(header::transfer_encoding);
		if( it != headers.end() and strtls::to_lower(*it->second) == "chunked" )
			m_impl->m_state = state_t::chunk;
		else
		{
			m_impl->m_content_length = body_size;
			m_impl->m_state = state_t::content_length;
			m_impl->m_headers[header::content_length] = m_impl->m_content_length;
		}
	}
	else
	{
		m_impl->m_content_length = *it->second.get<size_t>().or_else();
		m_impl->m_state = state_t::content_length;
	}
	return serialize_headers(headers);
}

version_enum generator_v11<protocol_model::base>::version() const noexcept
{
	return version_enum::v11;
}

} //namespace riwo::http
