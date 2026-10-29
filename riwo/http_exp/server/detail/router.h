// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_ROUTER_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_ROUTER_H

namespace riwo::http_exp
{

route_pattern::route_pattern(core_concepts::text_p<char> auto &&pattern) :
	m_impl(compile(strtls::to_string(std::forward<decltype(pattern)>(pattern))))
{

}

optional<route_match> route_pattern::match(const core_concepts::text_p<char> auto &path) const noexcept
{
	return match_impl(strtls::to_view(path));
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SERVER_DETAIL_ROUTER_H
