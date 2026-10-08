// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_TOOLS_H
#define RIWO_CORE_CXX_DETAIL_TOOLS_H

namespace riwo
{

template <typename T>
const char *type_name()
{
	return type_name(typeid(T));
}

const char *type_name(auto &&t)
{
	return type_name(typeid(t));
}

template <typename T>
constexpr T &remove_const(const T &v)
{
	return const_cast<T&>(v);
}

template <typename T>
constexpr T *remove_const(const T *v)
{
	return const_cast<T*>(v);
}

template <typename T>
constexpr const T &as_const(const T &v)
{
	return v;
}

template <typename T>
constexpr const T &&as_const(const T &&v)
{
	return std::move(v);
}

template <typename T>
constexpr const T *as_const(const T *v)
{
	return v;
}

constexpr decltype(auto) return_reference(auto &&value)
{
	return std::forward<decltype(value)>(value);
}

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_TOOLS_H
