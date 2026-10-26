// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_METHOD_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_METHOD_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp
{

method::method(core_concepts::string_p<char> auto &&token)
{
	initialize(strtls::to_string(std::forward<decltype(token)>(token)));
}

method::method(core_concepts::string_p<char> auto &&token, method_properties properties)
{
	initialize(strtls::to_string(std::forward<decltype(token)>(token)), properties);
}

} //namespace riwo::http_exp

template <>
struct std::hash<riwo::http_exp::method> {
	[[nodiscard]] size_t operator()(const riwo::http_exp::method &value) const noexcept;
};

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_METHOD_H
