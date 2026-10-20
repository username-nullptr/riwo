// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "fields.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool token_character(unsigned char value) noexcept
{
	return std::isalnum(value) != 0 or std::string_view("!#$%&'*+-.^_`|~").find(
		static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() and std::ranges::equal(lhs, rhs, [](char left, char right) {
		return std::tolower(static_cast<unsigned char>(left)) ==
			std::tolower(static_cast<unsigned char>(right));
	});
}

void validate(std::string_view name, std::string_view value)
{
	if( name.empty() or not std::ranges::all_of(name, [](unsigned char byte) {
		return token_character(byte);
	}))
		throw std::invalid_argument("Invalid HTTP field name");

	if( value.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos )
		throw std::invalid_argument("Invalid HTTP field value");
}

} //namespace

field_range::const_iterator::const_iterator(base_iterator current,
	base_iterator end, std::string_view name) noexcept :
	m_current(current), m_end(end), m_name(name)
{
	skip();
}

void field_range::const_iterator::skip() noexcept
{
	while( m_current != m_end and not equal_name(m_current->name, m_name) )
		++m_current;
}

field_range::const_iterator::reference
field_range::const_iterator::operator*() const noexcept {
	return *m_current;
}

field_range::const_iterator::pointer
field_range::const_iterator::operator->() const noexcept {
	return std::addressof(*m_current);
}

field_range::const_iterator &field_range::const_iterator::operator++() noexcept
{
	++m_current;
	skip();
	return *this;
}

field_range::const_iterator field_range::const_iterator::operator++(int) noexcept
{
	auto previous = *this;
	++*this;
	return previous;
}

field_range::field_range(const std::vector<field> &values, std::string name) :
	m_values(std::addressof(values)), m_name(std::move(name))
{
}

field_range::const_iterator field_range::begin() const noexcept {
	return {m_values->begin(), m_values->end(), m_name};
}

field_range::const_iterator field_range::end() const noexcept {
	return {m_values->end(), m_values->end(), m_name};
}

void fields::add(std::string name, std::string value)
{
	validate(name, value);
	m_values.push_back({std::move(name), std::move(value)});
}

void fields::set(std::string name, std::string value)
{
	validate(name, value);
	auto first = std::ranges::find_if(m_values, [&](const field &item) {
		return equal_name(item.name, name);
	});
	if( first == m_values.end() )
	{
		m_values.push_back({std::move(name), std::move(value)});
		return;
	}
	first->name = std::move(name);
	first->value = std::move(value);
	m_values.erase(std::remove_if(std::next(first), m_values.end(), [&](const field &item) {
		return equal_name(item.name, first->name);
	}), m_values.end());
}

void fields::erase(std::string_view name)
{
	m_values.erase(std::remove_if(m_values.begin(), m_values.end(), [&](const field &item) {
		return equal_name(item.name, name);
	}), m_values.end());
}

field_range fields::find_all(std::string_view name) const {
	return {m_values, std::string(name)};
}

optional<std::string_view> fields::find_first(std::string_view name) const
{
	const auto found = std::ranges::find_if(m_values, [&](const field &item) {
		return equal_name(item.name, name);
	});
	return found == m_values.end() ? optional<std::string_view>{} : found->value;
}

fields::const_iterator fields::begin() const noexcept {
	return m_values.begin();
}

fields::const_iterator fields::end() const noexcept {
	return m_values.end();
}

bool fields::empty() const noexcept {
	return m_values.empty();
}

size_t fields::size() const noexcept {
	return m_values.size();
}

} //namespace riwo::http_exp
