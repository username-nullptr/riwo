// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_ROUTER_H
#define RIWO_EXP_HTTP_SERVER_ROUTER_H

#include <riwo/experimental/http/protocol/parameters.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

struct route_match
{
	parameter_view path_parameters;
	size_t specificity = 0;

	// Lower values are more specific among routes with equal specificity.
	size_t wildcard_rank = 0;
};

// Patterns are matched against the path only (never the query). `*` matches
// zero or more path characters, `?` one path character, and `{name}` one
// non-empty path segment. Literal routes always outrank patterns.
class RIWO_EXP_HTTP_API route_pattern
{
public:
	explicit route_pattern(core_concepts::text_p<char> auto &&pattern);
	~route_pattern();

	route_pattern(const route_pattern &other);
	route_pattern &operator=(const route_pattern &other);

	route_pattern(route_pattern &&other) noexcept;
	route_pattern &operator=(route_pattern &&other) noexcept;

public:
	[[nodiscard]] optional<route_match> match (
		const core_concepts::text_p<char> auto &path
	) const noexcept;

	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] bool exact() const noexcept;

private:
	class impl;
	[[nodiscard]] static std::shared_ptr<const impl> compile(std::string pattern);
	[[nodiscard]] optional<route_match> match_impl(std::string_view path) const noexcept;
	std::shared_ptr<const impl> m_impl {};
};

} //namespace riwo::experimental::http
#include <riwo/experimental/http/server/detail/router.h>


#endif //RIWO_EXP_HTTP_SERVER_ROUTER_H
