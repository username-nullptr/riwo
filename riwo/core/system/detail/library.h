// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_DETAIL_LIBRARY_H
#define RIWO_CORE_SYSTEM_DETAIL_LIBRARY_H

namespace riwo
{

template <concepts::function Func>
auto library::interface(std::string_view if_name) const
{
	using function_t = std::function<typename function_traits<Func>::call_type>;
	using pointer_t = typename function_traits<Func>::pointer_type;

	return interface(if_name).transform([](void *ptr) {
		return function_t(reinterpret_cast<pointer_t>(ptr));
	});
}

template <concepts::function Func, typename Arg0, typename...Args>
auto library::interface(std::format_string<Arg0,Args...> fmt_value, Arg0 &&arg0, Args&&...args) const
{
	return interface<Func>(std::format(
		fmt_value, std::forward<Arg0>(arg0), std::forward<Args>(args)...
	));
}

} //namespace riwo


#endif //RIWO_CORE_SYSTEM_DETAIL_LIBRARY_H
