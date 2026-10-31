// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_METHOD_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_METHOD_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

method::method(core_concepts::string_p<char> auto &&token)
{
	initialize(strtls::to_string(std::forward<decltype(token)>(token)));
}

method::method(core_concepts::string_p<char> auto &&token, method_properties properties)
{
	initialize(strtls::to_string(std::forward<decltype(token)>(token)), properties);
}

} //namespace riwo::experimental::http

template <>
struct std::hash<riwo::experimental::http::method> {
	[[nodiscard]] size_t operator()(const riwo::experimental::http::method &value) const noexcept;
};

#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_METHOD_H
