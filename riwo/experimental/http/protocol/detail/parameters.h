// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_PARAMETERS_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_PARAMETERS_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

optional<value> parameter_view::find_first(core_concepts::text_p<char> auto &&name) const
{
	return find_first_impl(strtls::to_view(std::forward<decltype(name)>(name)));
}

std::vector<value> parameter_view::find_all(core_concepts::text_p<char> auto &&name) const
{
	return find_all_impl(strtls::to_view(std::forward<decltype(name)>(name)));
}

bool parameter_view::contains(core_concepts::text_p<char> auto &&name) const noexcept
{
	return contains_impl(strtls::to_view(std::forward<decltype(name)>(name)));
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_PARAMETERS_H
