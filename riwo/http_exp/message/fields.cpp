// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "fields.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool token_character(unsigned char value) noexcept
{
	return std::isalnum(value) != 0 || std::string_view("!#$%&'*+-.^_`|~").find(
		static_cast<char>(value)) != std::string_view::npos;
}

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() && std::ranges::equal(lhs, rhs, [](char left, char right) {
		return std::tolower(static_cast<unsigned char>(left)) ==
			std::tolower(static_cast<unsigned char>(right));
	});
}

void validate(std::string_view name, std::string_view value)
{
	if( name.empty() || !std::ranges::all_of(name, [](unsigned char byte) {
		return token_character(byte);
	}))
		throw std::invalid_argument("Invalid HTTP field name");

	if( value.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos )
		throw std::invalid_argument("Invalid HTTP field value");
}

} //namespace

field_range::const_iterator::const_iterator(base_iterator current,
	base_iterator end, std::string_view name) noexcept :
	current_(current), end_(end), name_(name)
{
	skip();
}

void field_range::const_iterator::skip() noexcept
{
	while( current_ != end_ && !equal_name(current_->name, name_) )
		++current_;
}

field_range::const_iterator::reference
field_range::const_iterator::operator*() const noexcept {
	return *current_;
}

field_range::const_iterator::pointer
field_range::const_iterator::operator->() const noexcept {
	return std::addressof(*current_);
}

field_range::const_iterator &field_range::const_iterator::operator++() noexcept
{
	++current_;
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
	values_(std::addressof(values)), name_(std::move(name))
{
}

field_range::const_iterator field_range::begin() const noexcept {
	return {values_->begin(), values_->end(), name_};
}

field_range::const_iterator field_range::end() const noexcept {
	return {values_->end(), values_->end(), name_};
}

void fields::add(std::string name, std::string value)
{
	validate(name, value);
	values_.push_back({std::move(name), std::move(value)});
}

void fields::set(std::string name, std::string value)
{
	validate(name, value);
	auto first = std::ranges::find_if(values_, [&](const field &item) {
		return equal_name(item.name, name);
	});
	if( first == values_.end() )
	{
		values_.push_back({std::move(name), std::move(value)});
		return;
	}
	first->name = std::move(name);
	first->value = std::move(value);
	values_.erase(std::remove_if(std::next(first), values_.end(), [&](const field &item) {
		return equal_name(item.name, first->name);
	}), values_.end());
}

void fields::erase(std::string_view name)
{
	values_.erase(std::remove_if(values_.begin(), values_.end(), [&](const field &item) {
		return equal_name(item.name, name);
	}), values_.end());
}

field_range fields::find_all(std::string_view name) const {
	return {values_, std::string(name)};
}

optional<std::string_view> fields::find_first(std::string_view name) const
{
	const auto found = std::ranges::find_if(values_, [&](const field &item) {
		return equal_name(item.name, name);
	});
	return found == values_.end() ? optional<std::string_view>{} : found->value;
}

} //namespace riwo::http_exp
