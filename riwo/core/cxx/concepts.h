// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_CONCEPTS_H
#define RIWO_CORE_CXX_CONCEPTS_H

#include <riwo/core/cxx/function_traits.h>
#include <concepts>
#include <chrono>

namespace riwo
{

template <typename T>
using is_float = std::is_floating_point<T>;

template <typename T>
constexpr bool is_float_v = is_float<T>::value;

template <typename T>
using is_bool = std::is_same<T, bool>;

template <typename T>
constexpr bool is_bool_v = is_bool<T>::value;

template <typename T0, typename T1>
using is_dsame = std::is_same<std::decay_t<T0>, T1>;

template <typename T0, typename T1>
constexpr bool is_dsame_v = is_dsame<std::decay_t<T0>, T1>::value;

template <typename T>
struct is_time_point : std::false_type {};

template <typename Clock, typename Duration>
struct is_time_point<std::chrono::time_point<Clock, Duration>> : std::true_type {};

template <typename T>
constexpr bool is_time_point_v = is_time_point<T>::value;

template <typename T>
struct is_duration : std::false_type {};

template <typename Rep, typename Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename T>
constexpr bool is_duration_v = is_duration<T>::value;

template <typename T>
struct is_time : std::disjunction<is_time_point<T>, is_duration<T>> {};

template <typename T>
constexpr bool is_time_v = is_time<T>::value;

namespace concepts
{

template <typename T>
concept arithmetic = std::is_arithmetic_v<T>;

template <typename T>
concept arithmetic_p = arithmetic<std::remove_cvref_t<T>>;

template <typename T>
concept integral = std::integral<T>;

template <typename T>
concept integral_p = integral<std::remove_cvref_t<T>>;

template <typename T>
concept floating = std::floating_point<T>;

template <typename T>
concept floating_p = floating<std::remove_cvref_t<T>>;

template <typename T>
concept enumerate = std::is_enum_v<T>;

template <typename T>
concept enumerate_p = enumerate<std::remove_cvref_t<T>>;

template <typename T>
concept pointer = std::is_pointer_v<T>;

template <typename T>
concept pointer_p = pointer<std::remove_cvref_t<T>>;

template <typename T>
concept rvalue_reference = std::is_rvalue_reference_v<T>;

template <typename T>
concept trivial = std::is_trivial_v<T>;

template <typename T>
concept trivially_constructible = std::is_trivially_constructible_v<T>;

template <typename T>
concept trivially_destructible = std::is_trivially_destructible_v<T>;

template <typename T>
concept trivially_copy_constructible = std::is_trivially_copy_constructible_v<T>;

template <typename T>
concept trivially_copyable = std::is_trivially_copyable_v<T>;

template <typename T, typename U>
concept trivially_assignable = std::is_trivially_assignable_v<T,U>;

template <typename T>
concept trivially_copy_assignable = std::is_trivially_copy_assignable_v<T>;

template <typename Func>
concept function = is_function_v<Func>;

template <typename T>
concept void_function = is_void_func_v<T>;

template <typename Func, typename...Args>
concept callable = requires(Func &&func, Args&&...args) {
	func(std::forward<Args>(args)...);
};

template <typename Func, typename Res, typename...Args>
concept callable_ret = requires(Func &&func, Args&&...args) {
	{ func(std::forward<Args>(args)...) } -> std::same_as<Res>;
};

template <typename Func, typename Res, typename...Args>
concept callable_noret = requires(Func &&func, Args&&...args) {
	requires not std::is_same_v<decltype(func(std::forward<Args>(args)...)), Res>;
};

template <typename Func, typename...Args>
concept callable_void = callable_ret<Func, void, Args...>;

template <typename Func, typename...Args>
concept callable_novoid = requires(Func &&func, Args&&...args) {
	requires not std::is_void_v<decltype(func(std::forward<Args>(args)...))>;
};

template <typename Func>
concept std_func_temp = requires(Func *func) {
	std::function<Func>(func);
};

template <typename Struct, typename...Args>
concept constructible = std::constructible_from<Struct,Args...>;

template <typename L, typename R>
concept assignable = std::assignable_from<L,R>;

template <typename T>
concept copyable = std::copyable<T>;

template <typename T>
concept copy_constructible = std::copy_constructible<T>;

template <typename T>
concept movable = std::movable<T>;

template <typename T>
concept move_constructible = std::move_constructible<T>;

template <typename T>
concept copymovable = copyable<T> and movable<T>;

template <typename T>
concept copymove_constructible = copy_constructible<T> and move_constructible<T>;

template <typename T>
concept copy_or_movable = copyable<T> or movable<T>;

template <typename T>
concept copy_or_move_constructible = copy_constructible<T> or move_constructible<T>;

template <typename T>
concept optional_value = copy_or_move_constructible<T>;

template <typename T>
concept optional_value_p = optional_value<std::remove_cvref_t<T>>;

template <typename T, typename Base>
concept base_of = std::is_base_of_v<Base,T>;

template <typename T, typename...Args>
concept all_types = std::conjunction_v<std::is_same<T,std::remove_cvref_t<Args>>...>;

template <typename T>
concept time_point = is_time_point_v<T>;

template <typename T>
concept time_point_p = time_point<std::remove_cvref_t<T>>;

template <typename T>
concept duration = is_duration_v<T>;

template <typename T>
concept duration_p = duration<std::remove_cvref_t<T>>;

template <typename T>
concept time = is_time_v<T>;

template <typename T>
concept time_p = time<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts


#endif //RIWO_CORE_CXX_CONCEPTS_H
