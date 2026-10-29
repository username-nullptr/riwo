// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "router.h"
#include <riwo/core/algorithm/misc.h>

namespace riwo::http_exp { namespace
{

enum class route_element_kind {
	literal, any, remainder, parameter
};

struct route_element
{
	route_element_kind kind {};
	char character = '\0';
};

} //namespace

class RIWO_DECL_HIDDEN route_pattern::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl() = default;

	std::string text {};
	std::vector<route_element> elements {};
	std::vector<std::string> argument_names {};

	size_t specificity = 0;
	size_t star_count = 0;
	size_t question_count = 0;
};

std::shared_ptr<const route_pattern::impl> route_pattern::compile(std::string pattern)
{
	if( pattern.empty() or not pattern.starts_with('/') )
		invalid_argument::loc_throw("riwo::http_exp::route_pattern: pattern must start with '/'");

	auto result = std::make_shared<impl>();
	result->text = std::move(pattern);

	result->elements.reserve(result->text.size());
	std::unordered_set<std::string> names;

	for(size_t index=0; index<result->text.size(); ++index)
	{
		const auto current = result->text[index];
		if( current == '*' )
		{
			result->elements.push_back({.kind = route_element_kind::remainder});
			result->star_count += 1;
		}
		else if( current == '?' )
		{
			result->elements.push_back({.kind = route_element_kind::any});
			result->question_count += 1;
		}
		else if( current == '{' )
		{
			const auto close = result->text.find('}', index + 1);
			if( close == std::string::npos or close == index + 1 )
				invalid_argument::loc_throw("riwo::http_exp::route_pattern: invalid path argument");

			auto name = result->text.substr(index + 1, close - index - 1);
			if( name.find_first_of("{}/") != std::string::npos or not names.emplace(name).second )
				invalid_argument::loc_throw("riwo::http_exp::route_pattern: invalid or duplicate argument");

			result->argument_names.emplace_back(std::move(name));
			result->elements.push_back({.kind = route_element_kind::parameter});
			index = close;
		}
		else
		{
			if( current == '}' )
				invalid_argument::loc_throw("riwo::http_exp::route_pattern: unmatched '}'");

			result->elements.push_back ({
				.kind = route_element_kind::literal,
				.character = current
			});
			result->specificity += 4;
		}
	}
	result->specificity += result->argument_names.size();

	if( result->star_count == 0 and result->question_count == 0 and result->argument_names.empty() )
		result->specificity += static_cast<size_t>(1) << 30U;
	return result;
}

route_pattern::~route_pattern() = default;

route_pattern::route_pattern(const route_pattern&) = default;

route_pattern &route_pattern::operator=(const route_pattern&) = default;

route_pattern::route_pattern(route_pattern &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

route_pattern &route_pattern::operator=(route_pattern &&other) noexcept
{
	if( this != &other )
		m_impl = std::move(other.m_impl);
	return *this;
}

std::string_view route_pattern::text() const noexcept
{
	return m_impl ? std::string_view(m_impl->text) : std::string_view{};
}

bool route_pattern::exact() const noexcept
{
	return m_impl and m_impl->star_count == 0 and
		m_impl->question_count == 0 and m_impl->argument_names.empty();
}

optional<route_match> route_pattern::match_impl(std::string_view path) const noexcept
{
	if( not m_impl )
		return {};
	try {
		if( m_impl->argument_names.empty() )
		{
			const auto wildcard_rank = wildcard_match(m_impl->text, path);
			if( wildcard_rank < 0 )
				return {};

			return route_match {
				.path_parameters = {},
				.specificity = m_impl->specificity,
				.wildcard_rank = static_cast<size_t>(wildcard_rank)
			};
		}
		const auto path_size = path.size();
		const auto width = path_size + 1;

		std::vector<uint8_t> matches(width * (m_impl->elements.size() + 1));
		matches[m_impl->elements.size() * width + path_size] = 1;

		std::vector segment_ends(width, path_size);
		size_t slash = path_size;

		for(size_t position=width; position-- > 0; )
		{
			if( position < path_size and path[position] == '/' )
				slash = position;
			segment_ends[position] = slash;
		}
		std::vector<size_t> prefix_counts(width + 1);

		for(size_t index=m_impl->elements.size(); index-- > 0; )
		{
			const auto &element = m_impl->elements[index];
			const auto row = index * width;
			const auto next_row = row + width;

			if( element.kind == route_element_kind::remainder )
			{
				uint8_t suffix_match = 0;
				for(size_t position=width; position-- > 0; )
				{
					suffix_match |= matches[next_row + position];
					matches[row + position] = suffix_match;
				}
				continue;
			}
			if( element.kind == route_element_kind::parameter )
			{
				for(size_t position=0; position<width; ++position)
					prefix_counts[position + 1] = prefix_counts[position] + matches[next_row + position];

				for(size_t position=0; position<path_size; ++position)
				{
					const auto end = segment_ends[position];
					if( prefix_counts[end + 1] != prefix_counts[position + 1] )
						matches[row + position] = 1;
				}
				continue;
			}
			for(size_t position=0; position<path_size; ++position)
			{
				const bool equal =
				    element.kind == route_element_kind::any or
					path[position] == element.character;

				matches[row + position] =
					equal and matches[next_row + position + 1];
			}
		}
		if( not matches[0] )
			return {};

		parameter_view::container_t parameters {};
		parameters.reserve(m_impl->argument_names.size());

		size_t argument_index = 0;
		size_t path_position = 0;

		for(size_t index=0; index<m_impl->elements.size(); ++index)
		{
			const auto &element = m_impl->elements[index];
			const auto next_row = (index + 1) * width;

			if( element.kind == route_element_kind::remainder )
			{
				for(size_t position=path_position; position<=path_size; ++position)
				{
					if( matches[next_row + position] )
						path_position = position;
				}
			}
			else if( element.kind == route_element_kind::parameter )
			{
				const auto begin = path_position;
				for(size_t position=begin + 1; position<=segment_ends[begin]; ++position)
				{
					if( matches[next_row + position] )
						path_position = position;
				}
				auto decoded = decode_uri_component (
					path.substr(begin, path_position - begin), false
				);
				if( not decoded )
					return {};

				parameters.emplace_back (
					m_impl->argument_names[argument_index++], std::move(*decoded)
				);
			}
			else
				++path_position;
		}
		return route_match {
			.path_parameters = parameter_view(std::move(parameters)),
			.specificity = m_impl->specificity,
			.wildcard_rank = static_cast<size_t>(wildcard_match_rank (
				m_impl->star_count, m_impl->question_count, path_size
			))
		};
	}
	catch(...) {}
	return {};
}

} //namespace riwo::http_exp
