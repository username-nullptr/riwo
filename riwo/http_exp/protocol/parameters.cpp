// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parameters.h"

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] int hex_value(unsigned char value) noexcept
{
	if( value >= '0' and value <= '9' ) return value - '0';
	if( value >= 'a' and value <= 'f' ) return value - 'a' + 10;
	if( value >= 'A' and value <= 'F' ) return value - 'A' + 10;
	return -1;
}

} //namespace

parameter_view::parameter_view(container_t values) noexcept :
	m_values(std::move(values))
{
}

optional<std::string_view> parameter_view::find_first(
	std::string_view name) const noexcept
{
	for( const auto &value : m_values )
		if( value.name == name ) return value.value;
	return {};
}

std::vector<std::string_view> parameter_view::find_all(
	std::string_view name) const
{
	std::vector<std::string_view> result;
	for( const auto &value : m_values )
		if( value.name == name ) result.emplace_back(value.value);
	return result;
}

bool parameter_view::contains(std::string_view name) const noexcept {
	return find_first(name).has_value();
}

bool parameter_view::empty() const noexcept { return m_values.empty(); }
size_t parameter_view::size() const noexcept { return m_values.size(); }

auto parameter_view::begin() const noexcept -> const_iterator {
	return m_values.begin();
}

auto parameter_view::end() const noexcept -> const_iterator {
	return m_values.end();
}

auto parameter_view::values() const noexcept -> const container_t& {
	return m_values;
}

optional<std::string> decode_uri_component(std::string_view value,
	bool plus_as_space) noexcept
{
	try
	{
		std::string result;
		result.reserve(value.size());
		for( size_t index = 0; index < value.size(); ++index )
		{
			const auto current = static_cast<unsigned char>(value[index]);
			if( current == '%' )
			{
				if( index + 2 >= value.size() ) return {};
				const auto high = hex_value(
					static_cast<unsigned char>(value[index + 1]));
				const auto low = hex_value(
					static_cast<unsigned char>(value[index + 2]));
				if( high < 0 or low < 0 ) return {};
				result.push_back(static_cast<char>((high << 4) | low));
				index += 2;
			}
			else if( current == '+' and plus_as_space ) result.push_back(' ');
			else result.push_back(static_cast<char>(current));
		}
		return result;
	}
	catch(...) { return {}; }
}

optional<parameter_view> parse_query_parameters(std::string_view query) noexcept
{
	try
	{
		if( query.starts_with('?') ) query.remove_prefix(1);
		parameter_view::container_t result;
		if( query.empty() ) return parameter_view(std::move(result));
		size_t begin = 0;
		for(;;)
		{
			const auto end = query.find('&', begin);
			const auto item = query.substr(begin,
				end == std::string_view::npos ? query.size() - begin : end - begin);
			const auto equal = item.find('=');
			auto name = decode_uri_component(item.substr(0, equal), true);
			auto value = decode_uri_component(equal == std::string_view::npos ?
				std::string_view{} : item.substr(equal + 1), true);
			if( not name or not value ) return {};
			result.push_back({std::move(*name), std::move(*value)});
			if( end == std::string_view::npos ) break;
			begin = end + 1;
		}
		return parameter_view(std::move(result));
	}
	catch(...) { return {}; }
}

} //namespace riwo::http_exp
