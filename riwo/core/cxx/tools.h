// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_TOOLS_H
#define RIWO_CORE_CXX_TOOLS_H

#include <riwo/core/cxx/aggregate_template.h>
#include <riwo/core/cxx/type_traits.h>
#include <riwo/core/cxx/attributes.h>
#include <typeinfo>

namespace riwo
{

using std_typeid_t = decltype(typeid(void).hash_code());

template <typename T>
[[nodiscard]] RIWO_CORE_TAPI const char *type_name();

[[nodiscard]] RIWO_CORE_API const char *type_name(const std::type_info &type);
[[nodiscard]] RIWO_CORE_TAPI const char *type_name(auto &&t);

template <typename T>
[[nodiscard]] constexpr T &remove_const(const T &v);

template <typename T>
[[nodiscard]] constexpr T *remove_const(const T *v);

template <typename T>
[[nodiscard]] constexpr const T &as_const(const T &v);

template <typename T>
[[nodiscard]] constexpr const T &&as_const(const T &&v);

template <typename T>
[[nodiscard]] constexpr const T *as_const(const T *v);

[[nodiscard]] constexpr decltype(auto) return_reference(auto &&value);

template <typename...Args>
constexpr void ignore_unused(Args&&...) {}

template <typename T, typename U> requires std::is_class_v<T>
struct class_member
{
	using type = std::remove_reference_t <
		decltype(std::declval<T>().*std::declval<U>())
	>;
};

template <typename T, typename U> requires std::is_class_v<T>
using class_member_t = class_member<T,U>::type;

template <typename Derived, typename Base>
struct crtp_derived { using type = Derived; };

template <typename Base>
struct crtp_derived<void,Base> { using type = Base; };

template <typename Derived, typename Base>
using crtp_derived_t = crtp_derived<Derived, Base>::type;

} //namespace riwo
#include <riwo/core/cxx/detail/tools.h>


#endif //RIWO_CORE_CXX_TOOLS_H
