// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_OPTIONAL_H
#define RIWO_CORE_CXX_OPTIONAL_H

#include <riwo/core/cxx/exception.h>
#include <riwo/core/cxx/concepts.h>
#include <optional>

namespace riwo
{

using std::bad_optional_access;
using std::nullopt_t;

constexpr nullopt_t nullopt = std::nullopt;

template <typename Value>
class optional;

namespace detail
{

template <typename T>
struct optional_traits;

template <typename Value>
struct optional_traits<std::optional<Value>> {
	using value_type = Value;
};

template <typename Value>
struct optional_traits<optional<Value>> {
	using value_type = Value;
};

template <typename T>
concept optional_specialization = requires {
	typename optional_traits<std::remove_cvref_t<T>>::value_type;
};

template <typename T>
using optional_value_t = optional_traits<std::remove_cvref_t<T>>::value_type;

} //namespace detail

#define RIWO_OPTIONAL_ERROR_IF(opt, fmt, ...) if( not opt ) \
	throw riwo::runtime_error(riwo::with_location(std::format(fmt,__VA_ARGS__)))

template <typename Value>
class RIWO_CORE_TAPI optional final : public std::optional<Value>
{
public:
	using base_t = std::optional<Value>;
	using value_type = Value;
	using value_t = Value;

	using base_t::base_t;
	using base_t::value_or;

public:
	constexpr optional() noexcept = default;
	constexpr optional(const optional&) = default;
	constexpr optional(optional&&) = default;

	constexpr optional &operator=(const optional&) = default;
	constexpr optional &operator=(optional&&) = default;

	constexpr optional(const base_t &other);
	constexpr optional(base_t &&other)
		noexcept(std::is_nothrow_move_constructible_v<base_t>);

	constexpr optional &operator=(const base_t &other);
	constexpr optional &operator=(base_t &&other) noexcept (
		std::is_nothrow_move_assignable_v<base_t>
	);
	constexpr optional &operator=(nullopt_t) noexcept;

	template <typename U = value_t>
	constexpr optional &operator=(U &&value) requires (
		not std::same_as<std::remove_cvref_t<U>,optional> and
		not std::same_as<std::remove_cvref_t<U>,base_t> and
		std::constructible_from<value_t,U> and
		std::is_assignable_v<value_t&,U>
	);

public:
	[[nodiscard]] constexpr value_t value_or() const &
		requires std::copy_constructible<value_t> and std::default_initializable<value_t>;

	[[nodiscard]] constexpr value_t value_or() &&
		requires std::move_constructible<value_t> and std::default_initializable<value_t>;

public:
	template <typename Func>
	constexpr auto and_then(Func &&func) &
		requires std::invocable<Func,value_t&>;

	template <typename Func>
	constexpr auto and_then(Func &&func) const &
		requires std::invocable<Func,const value_t&>;

	template <typename Func>
	constexpr auto and_then(Func &&func) &&
		requires std::invocable<Func,value_t&&>;

	template <typename Func>
	constexpr auto and_then(Func &&func) const &&
		requires std::invocable<Func,const value_t&&>;

public:
	template <typename Func>
	constexpr auto transform(Func &&func) &
		requires std::invocable<Func,value_t&>;

	template <typename Func>
	constexpr auto transform(Func &&func) const &
		requires std::invocable<Func,const value_t&>;

	template <typename Func>
	constexpr auto transform(Func &&func) &&
		requires std::invocable<Func,value_t&&>;

	template <typename Func>
	constexpr auto transform(Func &&func) const &&
		requires std::invocable<Func,const value_t&&>;

public:
	template <typename Func>
	constexpr optional or_else(Func &&func) &
		requires std::invocable<Func>;

	template <typename Func>
	constexpr optional or_else(Func &&func) const &
		requires std::invocable<Func>;

	template <typename Func>
	constexpr optional or_else(Func &&func) &&
		requires std::invocable<Func>;

	template <typename Func>
	constexpr optional or_else(Func &&func) const &&
		requires std::invocable<Func>;

	[[nodiscard]] constexpr optional or_else(value_t value) const &
		requires std::copy_constructible<value_t>;

	[[nodiscard]] constexpr optional or_else(value_t value) &&
		requires std::move_constructible<value_t>;

	[[nodiscard]] constexpr optional or_else() const &
		requires std::copy_constructible<value_t> and std::default_initializable<value_t>;

	[[nodiscard]] constexpr optional or_else() &&
		requires std::move_constructible<value_t> and std::default_initializable<value_t>;
};

template <typename Value>
optional(Value) -> optional<Value>;

template <typename Value>
optional(std::optional<Value>) -> optional<Value>;

template <typename Value>
[[nodiscard]] constexpr auto make_optional(Value &&value);

template <typename Value, typename...Args>
[[nodiscard]] constexpr optional<Value> make_optional(Args&&...args)
	requires std::constructible_from<Value,Args...>;

template <typename Value, typename U, typename...Args>
[[nodiscard]] constexpr optional<Value> make_optional(std::initializer_list<U> list, Args&&...args)
	requires std::constructible_from<Value,std::initializer_list<U>&,Args...>;

template <typename>
struct is_optional : std::false_type {};

template <typename Value>
struct is_optional<std::optional<Value>> : std::true_type {};

template <typename Value>
struct is_optional<optional<Value>> : std::true_type {};

template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

namespace concepts
{

template <typename T>
concept optional = is_optional_v<T>;

template <typename T>
concept optional_p = optional<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts
#include <riwo/core/cxx/detail/optional.h>


#endif //RIWO_CORE_CXX_OPTIONAL_H
