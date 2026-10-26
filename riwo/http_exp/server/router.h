// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_ROUTER_H
#define RIWO_HTTP_EXP_SERVER_ROUTER_H

#include <riwo/http_exp/protocol/parameters.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp
{

struct route_match
{
	parameter_view path_parameters;
	size_t specificity = 0;
};

// Patterns are matched against the path only (never the query). `*` matches
// zero or more path characters, `?` one path character, and `{name}` one
// non-empty path segment. Literal routes always outrank patterns.
class RIWO_HTTP_EXP_API route_pattern
{
public:
	explicit route_pattern(core_concepts::text_p<char> auto &&pattern);
	~route_pattern();
	route_pattern(const route_pattern &other);
	route_pattern &operator=(const route_pattern &other);
	route_pattern(route_pattern &&) noexcept;
	route_pattern &operator=(route_pattern &&) noexcept;

	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] bool exact() const noexcept;
	[[nodiscard]] optional<route_match>
	match(std::string_view path) const noexcept;

private:
	class impl;
	[[nodiscard]] static std::shared_ptr<const impl>
	compile(std::string pattern);
	std::shared_ptr<const impl> m_impl;
};

route_pattern::route_pattern(core_concepts::text_p<char> auto &&pattern) :
	m_impl(compile(strtls::to_string(
		std::forward<decltype(pattern)>(pattern))))
{
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_ROUTER_H
