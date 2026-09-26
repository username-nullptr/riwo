// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_URL_H
#define RIWO_CORE_DETAIL_URL_H

namespace riwo
{

template <typename Arg0, typename...Args>
url::url(format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args) :
	url(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

template <typename Arg0, typename...Args>
url &url::emplace(format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args)
{
	return emplace(std::format(
		fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...
	));
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_URL_H
