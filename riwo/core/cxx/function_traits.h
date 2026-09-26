// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_FUNCTION_TRAITS_H
#define RIWO_CORE_CXX_FUNCTION_TRAITS_H

#include <functional>

namespace riwo
{

template <typename T>
struct is_function_ptr
{
	static constexpr bool value =
		std::is_pointer_v<T> and std::is_function_v<std::remove_pointer_t<T>>;
};

template <typename T>
constexpr bool is_function_ptr_v = is_function_ptr<T>::value;

struct operator_call_helper {
	void operator()(...) {}
};

template <typename T>
struct helper_composed : T, operator_call_helper {};

template <void(operator_call_helper::*)(...)>
struct member_function_holder {};

template <typename, typename = member_function_holder<&operator_call_helper::operator()>>
struct is_functor_impl : std::true_type {};

template <typename T>
struct is_functor_impl<T, member_function_holder<&helper_composed<T>::operator()>> : std::false_type {};

template <typename T>
struct is_functor : std::conditional_t<std::is_class_v<T>, is_functor_impl<T>, std::false_type> {};

template <typename R, typename...Args>
struct is_functor<R(*)(Args...)> : std::true_type {};

template <typename R, typename... Args>
struct is_functor<R(&)(Args...)> : std::true_type {};

template <typename R, typename... Args>
struct is_functor<R(*)(Args...) noexcept> : std::true_type {};

template <typename R, typename... Args>
struct is_functor<R(&)(Args...) noexcept> : std::true_type {};

template <typename T>
constexpr bool is_functor_v = is_functor<T>::value;

template <typename T>
struct function_traits : function_traits<decltype(&T::operator())>
{
	using type = T;
	static constexpr bool is_member_func = false;
};

// template <typename T>
// struct function_traits : function_traits<decltype(&T::operator() noexcept)>
// {
// 	using type = T;
// 	static constexpr bool is_member_func = false;
// };

template <typename R, typename...Args>
struct function_traits_base
{
	static constexpr std::size_t arg_count = sizeof...(Args);
	using base_type = R(Args...);

	using return_type = R;
	using arg_types = std::tuple<Args...>;

	template <size_t N> requires (N < arg_count)
	struct arg_type {
		using type = std::tuple_element_t<N, arg_types>;
	};

	template <size_t N> requires (N < arg_count)
	using arg_type_t = arg_type<N>::type;

	using pointer_type = R(*)(Args...);
	using call_type = R(Args...);
};

template <typename R, typename...Args>
struct function_traits<R(Args...)> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename...Args>
struct function_traits<R(*)(Args...)> : function_traits_base<R,Args...>
{
	using type = R(*)(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename...Args>
struct function_traits<R(&)(Args...)> : function_traits_base<R,Args...>
{
	using type = R(&)(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...)> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) noexcept> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename...Args>
struct function_traits<R(*)(Args...) noexcept> : function_traits_base<R,Args...>
{
	using type = R(*)(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename...Args>
struct function_traits<R(&)(Args...) noexcept> : function_traits_base<R,Args...>
{
	using type = R(&)(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) noexcept> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) const> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) const> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) volatile> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) volatile> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) const noexcept> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) const noexcept> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) volatile noexcept> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) volatile noexcept> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) const volatile> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) const volatile> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename R, typename...Args>
struct function_traits<R(Args...) const volatile noexcept> : function_traits_base<R,Args...>
{
	using type = R(Args...);
	static constexpr bool is_member_func = false;
};

template <typename R, typename C, typename...Args>
struct function_traits<R(C::*)(Args...) const volatile noexcept> : function_traits_base<R,Args...>
{
	using type = R(C::*)(Args...);
	using class_t = C;
	static constexpr bool is_member_func = true;
};

template <typename T>
struct function_traits<std::function<T>> : function_traits<T> {};

template <typename F, std::size_t Index>
struct param_types {
	using type = std::tuple_element_t<Index, typename function_traits<F>::arg_types>;
};

template <typename F, std::size_t Index>
using param_types_t = param_types<F, Index>::type;

template <typename F>
struct is_function : std::disjunction <
	std::is_member_function_pointer<F>, std::is_function<F>, is_functor<F>
> {};

template <typename F>
constexpr bool is_function_v = is_function<F>::value;

namespace detail
{

template <typename F> // Fucking msvc !!!
constexpr bool is_void_func_helper_v = std::is_void_v<typename function_traits<F>::return_type>;

} //namespace detail

template <typename F>
struct is_void_func
{
	// Fucking msvc !!!
	static constexpr bool value = []() consteval -> bool
	{
		if constexpr( is_function_v<F> )
			return detail::is_void_func_helper_v<F>;
		else
			return false;
	}();
};

template <typename F>
constexpr bool is_void_func_v = is_void_func<F>::value;

} //namespace riwo


#endif //RIWO_CORE_CXX_FUNCTION_TRAITS_H
