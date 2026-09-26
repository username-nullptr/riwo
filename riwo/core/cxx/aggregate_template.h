// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_AGGREGATE_TEMPLATE_H
#define RIWO_CORE_CXX_AGGREGATE_TEMPLATE_H

#include <variant>
#include <tuple>

namespace riwo
{

template<typename T, typename... Args>
struct has_tof_args : std::disjunction<std::is_same<T, Args>...> {};

template<typename T, typename... Args>
constexpr bool has_tof_args_v = has_tof_args<T, Args...>::value;

template <typename>
struct is_tuple : std::false_type {};

template <typename... Args>
struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T>
constexpr bool is_tuple_v = is_tuple<T>::value;

template <typename...>
struct is_variant : std::false_type {};

template <typename...Args>
struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename...Args>
constexpr bool is_variant_v = is_variant<Args...>::value;

template <typename Agg, typename T>
struct is_contained_in : std::false_type {};

template <typename T, typename...Args>
struct is_contained_in<std::tuple<Args...>,T> : has_tof_args<T,Args...> {};

template <typename T, typename...Args>
struct is_contained_in<std::variant<Args...>,T> : has_tof_args<T,Args...> {};

template <typename T, typename...Args>
constexpr bool is_contained_in_v = is_contained_in<T,Args...>::value;

template<typename T>
struct remove_repeat;

template<>
struct remove_repeat<std::tuple<>> {
	using type = std::tuple<>;
};

template<typename...Args>
struct remove_repeat<std::tuple<Args...>>
{
	template<typename Tup0, typename Tup1>
	struct remove_repeat_helper;

	template<typename FArgs, typename...RArgs, typename...SaveArgs>
	struct remove_repeat_helper<std::tuple<FArgs, RArgs...>, std::tuple<SaveArgs...>>
	{
		using inn_type = std::conditional_t <
			has_tof_args_v<FArgs, SaveArgs...>,
			std::tuple<SaveArgs...>, std::tuple<FArgs, SaveArgs...>
		>;
		using type = remove_repeat_helper<std::tuple<RArgs...>, inn_type>::type;
	};

	template<typename...SaveArgs>
	struct remove_repeat_helper<std::tuple<>, std::tuple<SaveArgs...>> {
		using type = std::tuple<SaveArgs...>;
	};

	using type = remove_repeat_helper<std::tuple<Args...>, std::tuple<>>::type;
};

template<typename...Args>
struct remove_repeat<std::variant<Args...>>
{
	template<typename Tup0, typename Tup1>
	struct remove_repeat_helper;

	template<typename FArgs, typename...RArgs, typename...SaveArgs>
	struct remove_repeat_helper<std::variant<FArgs, RArgs...>, std::variant<SaveArgs...>>
	{
		using inn_type = std::conditional_t <
			has_tof_args_v<FArgs, SaveArgs...>,
			std::variant<SaveArgs...>, std::variant<FArgs, SaveArgs...>
		>;
		using type = remove_repeat_helper<std::variant<RArgs...>, inn_type>::type;
	};

	template<typename...SaveArgs>
	struct remove_repeat_helper<std::variant<>, std::variant<SaveArgs...>> {
		using type = std::variant<SaveArgs...>;
	};

	using type = remove_repeat_helper<std::variant<Args...>, std::variant<>>::type;
};

template<typename T>
using remove_repeat_t = remove_repeat<T>::type;

} //namespace riwo


#endif //RIWO_CORE_CXX_AGGREGATE_TEMPLATE_H
