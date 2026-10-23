// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "fields.h"
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool token_character(unsigned char value) noexcept
{
	return std::isalnum(value) != 0 or std::string_view("!#$%&'*+-.^_`|~").find(
		static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
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

class field_range::const_iterator::impl
{
public:
	impl(base_iterator_t current_value, base_iterator_t end_value,
		std::string_view name_value) noexcept :
		current(current_value), end(end_value), name(name_value)
	{
		skip();
	}

	void skip() noexcept
	{
		while( current != end and not equal_name(current->name, name) )
			++current;
	}

	base_iterator_t current;
	base_iterator_t end;
	std::string_view name;
};

field_range::const_iterator::const_iterator() = default;

field_range::const_iterator::const_iterator(base_iterator_t current,
	base_iterator_t end, std::string_view name) noexcept :
	m_impl(std::make_unique<impl>(current, end, name))
{
}

field_range::const_iterator::~const_iterator() = default;

field_range::const_iterator::const_iterator(const const_iterator &other) :
	m_impl(other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr)
{
}

field_range::const_iterator &field_range::const_iterator::operator=(
	const const_iterator &other)
{
	if( this == &other )
		return *this;
	auto value = other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr;
	m_impl = std::move(value);
	return *this;
}

field_range::const_iterator::const_iterator(const_iterator &&other) noexcept =
	default;

field_range::const_iterator &field_range::const_iterator::operator=(
	const_iterator &&other) noexcept = default;

field_range::const_iterator::reference
field_range::const_iterator::operator*() const noexcept {
	return *m_impl->current;
}

field_range::const_iterator::pointer
field_range::const_iterator::operator->() const noexcept {
	return std::addressof(*m_impl->current);
}

field_range::const_iterator &field_range::const_iterator::operator++() noexcept
{
	++m_impl->current;
	m_impl->skip();
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
	if( not lhs.m_impl or not rhs.m_impl )
		return not lhs.m_impl and not rhs.m_impl;
	return lhs.m_impl->current == rhs.m_impl->current and
		lhs.m_impl->end == rhs.m_impl->end and
		lhs.m_impl->name == rhs.m_impl->name;
}

class field_range::impl
{
public:
	impl(const std::vector<field> &values, std::string name_value) :
		m_values(std::addressof(values)), m_name(std::move(name_value))
	{
	}

	const std::vector<field> *m_values;
	std::string m_name;
};

field_range::field_range(const std::vector<field> &values, std::string name) :
	m_impl(std::make_shared<impl>(values, std::move(name)))
{
}

field_range::field_range(field_range &&other) noexcept : m_impl(other.m_impl) {}

field_range &field_range::operator=(field_range &&other) noexcept
{
	if( this != &other )
		m_impl = other.m_impl;
	return *this;
}

field_range::const_iterator field_range::begin() const noexcept {
	return {m_impl->m_values->begin(), m_impl->m_values->end(), m_impl->m_name};
}

field_range::const_iterator field_range::end() const noexcept {
	return {m_impl->m_values->end(), m_impl->m_values->end(), m_impl->m_name};
}

class fields::impl
{
public:
	using container_t = fields::container_t;

	impl() = default;
	explicit impl(container_t values) : m_values(std::move(values)) {}

	container_t m_values;
};

fields::fields() : m_impl(std::make_unique<impl>()) {}

fields::~fields() = default;

fields::fields(const fields &other) :
	m_impl(std::make_unique<impl>(other.m_impl->m_values))
{
}

fields &fields::operator=(const fields &other)
{
	if( this == &other )
		return *this;
	auto value = std::make_unique<impl>(other.m_impl->m_values);
	m_impl = std::move(value);
	return *this;
}

fields::fields(fields &&other) noexcept : m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>();
}

fields &fields::operator=(fields &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>();
	return *this;
}

fields &fields::add(std::string name, riwo::value value)
{
	validate(name, value.to_string());
	m_impl->m_values.push_back({std::move(name), std::move(value)});
	return *this;
}

fields &fields::set(std::string name, riwo::value value)
{
	validate(name, value.to_string());
	auto &values = m_impl->m_values;
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
	auto &values = m_impl->m_values;
	values.erase(std::remove_if(values.begin(), values.end(), [&](const field &item) {
		return equal_name(item.name, name);
	}), values.end());
	return *this;
}

field_range fields::find_all(std::string_view name) const {
	return {m_impl->m_values, std::string(name)};
}

optional<std::string_view> fields::find_first(std::string_view name) const
{
	const auto &values = m_impl->m_values;
	const auto found = std::ranges::find_if(values, [&](const field &item) {
		return equal_name(item.name, name);
	});
	return found == values.end() ? optional<std::string_view>{} :
		std::string_view(found->value.to_string());
}

fields::const_iterator fields::begin() const noexcept {
	return m_impl->m_values.begin();
}

fields::const_iterator fields::end() const noexcept {
	return m_impl->m_values.end();
}

bool fields::empty() const noexcept {
	return m_impl->m_values.empty();
}

size_t fields::size() const noexcept {
	return m_impl->m_values.size();
}

} //namespace riwo::http_exp
