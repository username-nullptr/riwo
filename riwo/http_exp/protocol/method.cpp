// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "method.h"
#include "detail/syntax.h"

namespace riwo::http_exp { namespace
{

struct known_method
{
	std::string_view text {};
	method_properties properties {};
};

constexpr std::array known_methods
{
	known_method {.text = "GET"    , .properties = {.safe = true , .idempotent = true }},
	known_method {.text = "HEAD"   , .properties = {.safe = true , .idempotent = true }},
	known_method {.text = "POST"   , .properties = {.safe = false, .idempotent = false}},
	known_method {.text = "PUT"    , .properties = {.safe = false, .idempotent = true }},
	known_method {.text = "PATCH"  , .properties = {.safe = false, .idempotent = false}},
	known_method {.text = "DELETE" , .properties = {.safe = false, .idempotent = true }},
	known_method {.text = "OPTIONS", .properties = {.safe = true , .idempotent = true }},
	known_method {.text = "TRACE"  , .properties = {.safe = true , .idempotent = true }},
	known_method {.text = "CONNECT", .properties = {.safe = false, .idempotent = false}}
};

[[nodiscard]] optional<method::well_known> identify(std::string_view token) noexcept
{
	for(size_t index=0; index<known_methods.size(); ++index)
	{
		if( known_methods[index].text == token )
			return static_cast<method::well_known>(index);
	}
	return nullopt;
}

[[nodiscard]] method_properties normalized(method_properties properties) noexcept
{
	if( properties.safe )
		properties.idempotent = true;
	return properties;
}

} //namespace

static void validate_token(std::string_view token)
{
	if( token.empty() or not std::ranges::all_of(
		token, [](unsigned char value) {
			return detail::ascii_token_character(value);
		}) )
		invalid_argument::loc_throw("Invalid HTTP method token");
}

method::method(well_known value) noexcept :
	m_properties(known_methods[static_cast<size_t>(value)].properties),
	m_known(value)
{

}

method::method(std::string_view token) :
	method(std::string(token))
{
	
}

method::method(std::string token) :
	m_token(std::move(token)), m_known(identify(m_token))
{
	validate_token(m_token);
	if( m_known )
	{
		m_properties = known_methods[static_cast<size_t>(*m_known)].properties;
		m_token.clear();
	}
}

method::method(const char *token) :
	method(std::string(token))
{

}

method::method(std::string_view token, method_properties properties) :
	method(std::string(token), properties)
{
	
}

method::method(std::string token, method_properties properties) :
	m_token(std::move(token)), m_properties(normalized(properties)),
	m_known(identify(m_token))
{
	validate_token(m_token);
	if( m_known )
	{
		const auto expected =
			known_methods[static_cast<size_t>(*m_known)].properties;

		if( m_properties != expected )
		{
			invalid_argument::loc_throw (
				"Conflicting properties for a standard HTTP method"
			);
		}
		m_properties = expected;
		m_token.clear();
	}
}

method::method(const char *token, method_properties properties) :
	method(std::string(token), properties)
{

}

method::method(method &&other) noexcept :
	m_token(std::move(other.m_token)),
	m_properties(std::exchange(other.m_properties, {})),
	m_known(std::exchange(other.m_known, nullopt))
{
	other.m_token.clear();
}

method &method::operator=(method &&other) noexcept
{
	if( this != &other )
	{
		m_token = std::move(other.m_token);
		m_properties = std::exchange(other.m_properties, {});
		m_known = std::exchange(other.m_known, nullopt);
		other.m_token.clear();
	}
	return *this;
}

std::string_view method::text() const noexcept
{
	return m_known ?
		known_methods[static_cast<size_t>(*m_known)].text :
		std::string_view(m_token);
}

optional<method::well_known> method::known() const noexcept
{
	return m_known;
}

bool method::is_safe() const noexcept
{
	return m_properties.safe;
}

bool method::is_idempotent() const noexcept
{
	return m_properties.idempotent;
}

bool operator==(const method &lhs, const method &rhs) noexcept
{
	return lhs.text() == rhs.text();
}

} //namespace riwo::http_exp

size_t std::hash<riwo::http_exp::method>::operator()
(const riwo::http_exp::method &value) const noexcept {
	return std::hash<std::string_view>{}(value.text());
}
