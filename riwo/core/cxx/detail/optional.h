// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_OPTIONAL_H
#define RIWO_CORE_CXX_DETAIL_OPTIONAL_H

namespace riwo { namespace detail
{

template <typename Self, typename Func>
constexpr auto optional_and_then(Self &&self, Func &&func)
{
	using result_t = std::remove_cvref_t<std::invoke_result_t <
		Func, decltype(*std::forward<Self>(self))
	>>;
	static_assert(detail::optional_specialization<result_t>,
		"optional::and_then callback must return an optional specialization"
	);
	if( self.has_value() )
		return std::invoke(std::forward<Func>(func), *std::forward<Self>(self));
	return result_t {};
}

template <typename Self, typename Func>
constexpr auto optional_transform(Self &&self, Func &&func)
{
	using result_t = std::remove_cv_t<std::invoke_result_t <
		Func, decltype(*std::forward<Self>(self))
	>>;
	static_assert(not std::is_void_v<result_t>,
		"optional::transform callback must return a value"
	);
	static_assert(std::is_object_v<result_t> and not std::is_array_v<result_t>);

	if( self.has_value() )
	{
		return optional<result_t> (
			std::in_place,
			std::invoke(std::forward<Func>(func), *std::forward<Self>(self))
		);
	}
	return optional<result_t> {};
}

template <typename Value, typename Self, typename Func>
constexpr optional<Value> optional_or_else(Self &&self, Func &&func)
{
	using result_t = std::invoke_result_t<Func>;
	if( self.has_value() )
		return optional(std::forward<Self>(self));

	if constexpr( std::is_void_v<result_t> )
	{
		std::invoke(std::forward<Func>(func));
		return optional<Value> {};
	}
	else
	{
		static_assert(detail::optional_specialization<result_t>,
			"optional::or_else callback must return void or an optional specialization"
		);
		static_assert(std::same_as<optional_value_t<result_t>,Value>,
			"optional::or_else callback must preserve the value type"
		);
		return optional(std::invoke(std::forward<Func>(func)));
	}
}

} //namespace detail

template <typename Value>
constexpr optional<Value>::optional(const base_t &other) :
	base_t(other)
{

}

template <typename Value>
constexpr optional<Value>::optional(base_t &&other)
	noexcept(std::is_nothrow_move_constructible_v<base_t>) :
	base_t(std::move(other))
{

}

template <typename Value>
constexpr optional<Value> &optional<Value>::operator=(const base_t &other)
{
	base_t::operator=(other);
	return *this;
}

template <typename Value>
constexpr optional<Value> &optional<Value>::operator=(base_t &&other)
	noexcept(std::is_nothrow_move_assignable_v<base_t>)
{
	base_t::operator=(std::move(other));
	return *this;
}

template <typename Value>
constexpr optional<Value> &optional<Value>::operator=(nullopt_t) noexcept
{
	base_t::operator=(std::nullopt);
	return *this;
}

template <typename Value>
template <typename U>
constexpr optional<Value> &optional<Value>::operator=(U &&value) requires (
	not std::same_as<std::remove_cvref_t<U>,optional> and
	not std::same_as<std::remove_cvref_t<U>,base_t> and
	std::constructible_from<value_t,U> and
	std::is_assignable_v<value_t&,U>
){
	base_t::operator=(std::forward<U>(value));
	return *this;
}

template <typename Value>
constexpr Value optional<Value>::value_or() const &
	requires std::copy_constructible<value_t> and std::default_initializable<value_t>
{
	return base_t::value_or(value_t {});
}

template <typename Value>
constexpr Value optional<Value>::value_or() &&
	requires std::move_constructible<value_t> and std::default_initializable<value_t>
{
	return std::move(static_cast<base_t&>(*this)).value_or(value_t {});
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::and_then(Func &&func) &
	requires std::invocable<Func,value_t&>
{
	return detail::optional_and_then(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::and_then(Func &&func) const &
	requires std::invocable<Func,const value_t&>
{
	return detail::optional_and_then(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::and_then(Func &&func) &&
	requires std::invocable<Func,value_t&&>
{
	return detail::optional_and_then(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::and_then(Func &&func) const &&
	requires std::invocable<Func,const value_t&&>
{
	return detail::optional_and_then(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::transform(Func &&func) &
	requires std::invocable<Func,value_t&>
{
	return detail::optional_transform(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::transform(Func &&func) const &
	requires std::invocable<Func,const value_t&>
{
	return detail::optional_transform(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::transform(Func &&func) &&
	requires std::invocable<Func,value_t&&>
{
	return detail::optional_transform(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr auto optional<Value>::transform(Func &&func) const &&
	requires std::invocable<Func,const value_t&&>
{
	return detail::optional_transform(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr optional<Value> optional<Value>::or_else(Func &&func) &
	requires std::invocable<Func>
{
	return detail::optional_or_else<value_t>(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr optional<Value> optional<Value>::or_else(Func &&func) const &
	requires std::invocable<Func>
{
	return detail::optional_or_else<value_t>(*this, std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr optional<Value> optional<Value>::or_else(Func &&func) &&
	requires std::invocable<Func>
{
	return detail::optional_or_else<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
template <typename Func>
constexpr optional<Value> optional<Value>::or_else(Func &&func) const &&
	requires std::invocable<Func>
{
	return detail::optional_or_else<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value>
constexpr optional<Value> optional<Value>::or_else(value_t value) const &
	requires std::copy_constructible<value_t>
{
	return this->has_value() ? *this : optional(std::move(value));
}

template <typename Value>
constexpr optional<Value> optional<Value>::or_else(value_t value) &&
	requires std::move_constructible<value_t>
{
	return this->has_value() ? std::move(*this) : optional(std::move(value));
}

template <typename Value>
constexpr optional<Value> optional<Value>::or_else() const &
	requires std::copy_constructible<value_t> and std::default_initializable<value_t>
{
	return or_else(value_t {});
}

template <typename Value>
constexpr optional<Value> optional<Value>::or_else() &&
	requires std::move_constructible<value_t> and std::default_initializable<value_t>
{
	return std::move(*this).or_else(value_t {});
}

template <typename Value>
constexpr auto make_optional(Value &&value)
{
	return optional<std::decay_t<Value>>(std::forward<Value>(value));
}

template <typename Value, typename...Args>
constexpr optional<Value> make_optional(Args&&...args)
	requires std::constructible_from<Value,Args...>
{
	return optional<Value>(std::in_place, std::forward<Args>(args)...);
}

template <typename Value, typename U, typename...Args>
constexpr optional<Value> make_optional(std::initializer_list<U> list, Args&&...args)
	requires std::constructible_from<Value,std::initializer_list<U>&,Args...>
{
	return optional<Value>(std::in_place, list, std::forward<Args>(args)...);
}

template <typename Value>
constexpr void swap(optional<Value> &left, optional<Value> &right)
	noexcept(noexcept(left.swap(right)))
{
	left.swap(right);
}

template <typename Value, typename OtherValue>
[[nodiscard]] constexpr bool operator==
(const optional<Value> &left, const optional<OtherValue> &right) requires requires {
	{ *left == *right } -> std::convertible_to<bool>;
}{
	using left_base_t = optional<Value>::base_t;
	using right_base_t = optional<OtherValue>::base_t;
	return static_cast<const left_base_t&>(left) ==
		static_cast<const right_base_t&>(right);
}

template <typename Value, std::three_way_comparable_with<Value> OtherValue>
[[nodiscard]] constexpr auto operator<=>
(const optional<Value> &left, const optional<OtherValue> &right)
{
	using left_base_t = optional<Value>::base_t;
	using right_base_t = optional<OtherValue>::base_t;
	return static_cast<const left_base_t&>(left) <=>
		static_cast<const right_base_t&>(right);
}

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_OPTIONAL_H
