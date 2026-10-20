// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "method.h"

namespace riwo::http_exp { namespace
{

struct known_method
{
	std::string_view text;
	method_properties properties;
};

constexpr std::array known_methods {
	known_method{"GET",     {true,  true}},
	known_method{"HEAD",    {true,  true}},
	known_method{"POST",    {false, false}},
	known_method{"PUT",     {false, true}},
	known_method{"PATCH",   {false, false}},
	known_method{"DELETE",  {false, true}},
	known_method{"OPTIONS", {true,  true}},
	known_method{"TRACE",   {true,  true}},
	known_method{"CONNECT", {false, false}}
};

[[nodiscard]] bool token_character(unsigned char value) noexcept
{
	return std::isalnum(value) != 0 or std::string_view("!#$%&'*+-.^_`|~").find(
		static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] optional<method::well_known> identify(std::string_view token) noexcept
{
	for(size_t index = 0; index < known_methods.size(); ++index)
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

method::method(well_known value) noexcept :
	m_token(known_methods[static_cast<size_t>(value)].text),
	m_properties(known_methods[static_cast<size_t>(value)].properties),
	m_known(value)
{
}

method::method(std::string token) : m_token(std::move(token)), m_known(identify(m_token))
{
	if( m_token.empty() or not std::ranges::all_of(m_token, [](unsigned char value) {
		return token_character(value);
	}))
		throw std::invalid_argument("Invalid HTTP method token");
	if( m_known )
		m_properties = known_methods[static_cast<size_t>(*m_known)].properties;
}

method::method(std::string token, method_properties properties) :
	m_token(std::move(token)), m_properties(normalized(properties)), m_known(identify(m_token))
{
	if( m_token.empty() or not std::ranges::all_of(m_token, [](unsigned char value) {
		return token_character(value);
	}))
		throw std::invalid_argument("Invalid HTTP method token");

	if( m_known )
	{
		const auto expected = known_methods[static_cast<size_t>(*m_known)].properties;
		if( m_properties != expected )
			throw std::invalid_argument("Conflicting properties for a standard HTTP method");
		m_properties = expected;
	}
}

std::string_view method::text() const noexcept {
	return m_token;
}

optional<method::well_known> method::known() const noexcept {
	return m_known;
}

bool method::is_safe() const noexcept {
	return m_properties.safe;
}

bool method::is_idempotent() const noexcept {
	return m_properties.idempotent;
}

bool operator==(const method &lhs, const method &rhs) noexcept {
	return lhs.m_token == rhs.m_token;
}

} //namespace riwo::http_exp

size_t std::hash<riwo::http_exp::method>::operator()(
	const riwo::http_exp::method &value) const noexcept
{
	return std::hash<std::string_view>{}(value.text());
}
