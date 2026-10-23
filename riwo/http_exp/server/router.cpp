// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "router.h"
#include <regex>

namespace riwo::http_exp
{

class route_pattern::impl
{
public:
	std::string text;
	std::regex expression;
	std::vector<std::string> argument_names;
	size_t specificity = 0;
	bool exact = true;
};

namespace
{

[[nodiscard]] bool regex_special(char value) noexcept
{
	constexpr std::string_view special = R"(\.^$|()[]+)";
	return special.find(value) != std::string_view::npos;
}

} //namespace

std::shared_ptr<const route_pattern::impl>
route_pattern::compile(std::string pattern)
{
	if( pattern.empty() or not pattern.starts_with('/') )
		throw std::invalid_argument(
			"riwo::http_exp::route_pattern: pattern must start with '/'");

	auto result = std::make_shared<route_pattern::impl>();
	result->text = std::move(pattern);
	std::string expression = "^";
	std::unordered_set<std::string> names;
	for( size_t index = 0; index < result->text.size(); ++index )
	{
		const auto current = result->text[index];
		if( current == '*' )
		{
			expression += ".*";
			result->exact = false;
		}
		else if( current == '?' )
		{
			expression += '.';
			result->exact = false;
		}
		else if( current == '{' )
		{
			const auto close = result->text.find('}', index + 1);
			if( close == std::string::npos or close == index + 1 )
				throw std::invalid_argument(
					"riwo::http_exp::route_pattern: invalid path argument");
			auto name = result->text.substr(index + 1, close - index - 1);
			if( name.find_first_of("{}/") != std::string::npos or
				not names.insert(name).second )
				throw std::invalid_argument(
					"riwo::http_exp::route_pattern: invalid or duplicate argument");
			result->argument_names.emplace_back(std::move(name));
			expression += "([^/]+)";
			result->exact = false;
			index = close;
		}
		else
		{
			if( current == '}' )
				throw std::invalid_argument(
					"riwo::http_exp::route_pattern: unmatched '}'");
			if( regex_special(current) )
				expression.push_back('\\');
			expression.push_back(current);
			result->specificity += 4;
		}
	}
	expression += '$';
	result->specificity += result->argument_names.size();
	if( result->exact )
		result->specificity += size_t(1) << 30U;
	result->expression = std::regex(expression,
		std::regex_constants::ECMAScript | std::regex_constants::optimize);
	return result;
}

route_pattern::route_pattern(std::string pattern) :
	m_impl(compile(std::move(pattern)))
{
}

route_pattern::~route_pattern() = default;
route_pattern::route_pattern(const route_pattern&) = default;
route_pattern &route_pattern::operator=(const route_pattern&) = default;
route_pattern::route_pattern(route_pattern&&) noexcept = default;
route_pattern &route_pattern::operator=(route_pattern&&) noexcept = default;

std::string_view route_pattern::text() const noexcept { return m_impl->text; }
bool route_pattern::exact() const noexcept { return m_impl->exact; }

optional<route_match> route_pattern::match(std::string_view path) const noexcept
{
	try
	{
		std::match_results<std::string_view::const_iterator> matches;
		if( not std::regex_match(path.begin(), path.end(), matches,
			m_impl->expression) )
			return {};
		parameter_view::container_t parameters;
		parameters.reserve(m_impl->argument_names.size());
		for( size_t index = 0; index < m_impl->argument_names.size(); ++index )
		{
			auto decoded = decode_uri_component(std::string_view(
				matches[index + 1].first, matches[index + 1].second), false);
			if( not decoded )
				return {};
			parameters.push_back({m_impl->argument_names[index],
				std::move(*decoded)});
		}
		return route_match {
			.path_parameters = parameter_view(std::move(parameters)),
			.specificity = m_impl->specificity
		};
	}
	catch(...) {
		return {};
	}
}

} //namespace riwo::http_exp
