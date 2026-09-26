// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CXX_DETAIL_CONTAINER_H
#define RIWO_HTTP_CXX_DETAIL_CONTAINER_H

namespace riwo::http
{

optional<value> value_map_get
(const value_map &map, const core_concepts::text_p<char> auto &key) noexcept
{
	auto it = map.find(strtls::to_string(key));
	return it == map.end() ? optional<value>() : riwo::make_optional(it->second);
}

inline optional<value> value_set_get(const value_set &set, const value &node) noexcept
{
	auto it = set.find(node);
	return it == set.end() ? optional<value>() : riwo::make_optional(*it);
}

} //namespace riwo::http


#endif //RIWO_HTTP_CXX_DETAIL_CONTAINER_H
