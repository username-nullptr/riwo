// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "fields.h"
#include "detail/syntax.h"
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

void validate(std::string_view name, std::string_view value)
{
	if( name.empty() or not std::ranges::all_of(name, [](unsigned char byte) {
		return detail::ascii_token_character(byte);
	}))
		throw std::invalid_argument("Invalid HTTP field name");

	if( value.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos )
		throw std::invalid_argument("Invalid HTTP field value");
}

} //namespace

field_range::const_iterator::const_iterator() = default;

field_range::const_iterator::const_iterator(base_iterator_t current,
	base_iterator_t end, std::string_view name) noexcept :
	m_current(current), m_end(end), m_name(name), m_initialized(true)
{
	skip();
}

field_range::const_iterator::~const_iterator() = default;

field_range::const_iterator::const_iterator(const const_iterator &other) = default;

field_range::const_iterator &field_range::const_iterator::operator=(
	const const_iterator &other) = default;

field_range::const_iterator::const_iterator(const_iterator &&other) noexcept =
	default;

field_range::const_iterator &field_range::const_iterator::operator=(
	const_iterator &&other) noexcept = default;

field_range::const_iterator::reference
field_range::const_iterator::operator*() const noexcept {
	return *m_current;
}

field_range::const_iterator::pointer
field_range::const_iterator::operator->() const noexcept {
	return std::addressof(*m_current);
}

void field_range::const_iterator::skip() noexcept
{
	while( m_current != m_end and not equal_name(m_current->name, m_name) )
		++m_current;
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

bool operator==(const field_range::const_iterator &lhs,
	const field_range::const_iterator &rhs) noexcept
{
	if( not lhs.m_initialized or not rhs.m_initialized )
		return lhs.m_initialized == rhs.m_initialized;
	return lhs.m_current == rhs.m_current and
		lhs.m_end == rhs.m_end and lhs.m_name == rhs.m_name;
}

field_range::field_range(const std::vector<field> &values, std::string name) :
	m_values(std::addressof(values)), m_name(std::move(name))
{
}

field_range::field_range(field_range &&other) noexcept :
	m_values(std::exchange(other.m_values, nullptr)),
	m_name(std::move(other.m_name))
{
	other.m_name.clear();
}

field_range &field_range::operator=(field_range &&other) noexcept
{
	if( this != &other )
	{
		m_values = std::exchange(other.m_values, nullptr);
		m_name = std::move(other.m_name);
		other.m_name.clear();
	}
	return *this;
}

field_range::const_iterator field_range::begin() const noexcept {
	return m_values ? const_iterator(m_values->begin(),
		m_values->end(), m_name) : const_iterator{};
}

field_range::const_iterator field_range::end() const noexcept {
	return m_values ? const_iterator(m_values->end(),
		m_values->end(), m_name) : const_iterator{};
}

fields::fields() = default;

fields::~fields() = default;

fields::fields(const fields &other) = default;

fields &fields::operator=(const fields &other) = default;

fields::fields(fields &&other) noexcept : m_values(std::move(other.m_values))
{
	other.m_values.clear();
}

fields &fields::operator=(fields &&other) noexcept
{
	if( this == &other )
		return *this;
	m_values = std::move(other.m_values);
	other.m_values.clear();
	return *this;
}

fields &fields::add(std::string name, riwo::value value)
{
	validate(name, value.to_string());
	m_values.push_back({std::move(name), std::move(value)});
	return *this;
}

fields &fields::set(std::string name, riwo::value value)
{
	validate(name, value.to_string());
	auto &values = m_values;
	auto first = std::ranges::find_if(values, [&](const field &item) {
		return equal_name(item.name, name);
	});
	if( first == values.end() )
	{
		values.push_back({std::move(name), std::move(value)});
		return *this;
	}
	first->name = std::move(name);
	first->value = std::move(value);
	values.erase(std::remove_if(std::next(first), values.end(), [&](const field &item) {
		return equal_name(item.name, first->name);
	}), values.end());
	return *this;
}

fields &fields::erase(std::string_view name)
{
	auto &values = m_values;
	values.erase(std::remove_if(values.begin(), values.end(), [&](const field &item) {
		return equal_name(item.name, name);
	}), values.end());
	return *this;
}

field_range fields::find_all(std::string_view name) const {
	return {m_values, std::string(name)};
}

optional<std::string_view> fields::find_first(std::string_view name) const
{
	const auto &values = m_values;
	const auto found = std::ranges::find_if(values, [&](const field &item) {
		return equal_name(item.name, name);
	});
	return found == values.end() ? optional<std::string_view>{} :
		std::string_view(found->value.to_string());
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
