// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CXX_CONTAINER_H
#define RIWO_HTTP_CXX_CONTAINER_H

#include <riwo/http/cxx/attributes.h>
#include <riwo/http/cxx/concepts.h>
#include <riwo/core/container.h>
#include <map>
#include <set>

namespace riwo::http
{

using key_t = std::string;

struct RIWO_HTTP_API less_case_insensitive {
	[[nodiscard]] bool operator()(const key_t &v1, const key_t &v2) const;
};

template <typename Value>
using map = std::map<key_t, Value, less_case_insensitive>;

template <typename Value>
using set = std::set<Value, less_case_insensitive>;

using value_map = map<value>;
using value_set = set<value>;

[[nodiscard]] RIWO_HTTP_TAPI optional<value> value_map_get (
	const value_map &map, const core_concepts::text_p<char> auto &key
) noexcept;

[[nodiscard]] RIWO_HTTP_VAPI optional<value> value_set_get (
	const value_set &set, const value &node
) noexcept;

} //namespace riwo::http
#include <riwo/http/cxx/detail/container.h>


#endif //RIWO_HTTP_CXX_CONTAINER_H
