// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "method.h"
#include "detail/syntax.h"

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

class method::impl
{
public:
	explicit impl(well_known value) :
		m_token(known_methods[static_cast<size_t>(value)].text),
		m_properties(known_methods[static_cast<size_t>(value)].properties),
		m_known(value)
	{
	}

	explicit impl(std::string token) :
		m_token(std::move(token)), m_known(identify(m_token))
	{
		validate_token(m_token);
		if( m_known )
			m_properties = known_methods[static_cast<size_t>(*m_known)].properties;
	}

	impl(std::string token, method_properties properties) :
		m_token(std::move(token)), m_properties(normalized(properties)),
		m_known(identify(m_token))
	{
		validate_token(m_token);
		if( m_known )
		{
			const auto expected =
				known_methods[static_cast<size_t>(*m_known)].properties;
			if( m_properties != expected )
				throw std::invalid_argument(
					"Conflicting properties for a standard HTTP method");
			m_properties = expected;
		}
	}

	std::string m_token;
	method_properties m_properties;
	optional<well_known> m_known;

private:
	static void validate_token(const std::string &token)
	{
		if( token.empty() or not std::ranges::all_of(
			token, [](unsigned char value) {
				return detail::ascii_token_character(value);
			}) )
			throw std::invalid_argument("Invalid HTTP method token");
	}
};

method::method(well_known value) noexcept :
	m_impl(std::make_shared<impl>(value))
{
}

method::method(std::string token) :
	m_impl(std::make_shared<impl>(std::move(token)))
{
}

method::method(std::string token, method_properties properties) :
	m_impl(std::make_shared<impl>(std::move(token), properties))
{
}

method::method(method &&other) noexcept : m_impl(std::move(other.m_impl)) {}

method &method::operator=(method &&other) noexcept
{
	if( this != &other )
		m_impl = std::move(other.m_impl);
	return *this;
}

std::string_view method::text() const noexcept {
	return m_impl ? std::string_view(m_impl->m_token) : std::string_view{};
}

optional<method::well_known> method::known() const noexcept {
	return m_impl ? m_impl->m_known : nullopt;
}

bool method::is_safe() const noexcept {
	return m_impl and m_impl->m_properties.safe;
}

bool method::is_idempotent() const noexcept {
	return m_impl and m_impl->m_properties.idempotent;
}

bool operator==(const method &lhs, const method &rhs) noexcept {
	return lhs.text() == rhs.text();
}

} //namespace riwo::http_exp

size_t std::hash<riwo::http_exp::method>::operator()(
	const riwo::http_exp::method &value) const noexcept
{
	return std::hash<std::string_view>{}(value.text());
}
