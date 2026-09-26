// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_EXPECTED_H
#define RIWO_CORE_CXX_EXPECTED_H

#include <riwo/core/cxx/attributes.h>
#include <riwo/core/cxx/concepts.h>
#include <exception>
#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
# include <expected>
# define RIWO_HAS_STD_EXPECTED  1
#else
# define RIWO_HAS_STD_EXPECTED  0
#endif

namespace riwo
{

#if RIWO_HAS_STD_EXPECTED

using std::bad_expected_access;
using std::unexpect_t;

inline constexpr unexpect_t unexpect = std::unexpect;

template <typename Error>
class unexpected final : public std::unexpected<Error>
{
public:
	using base_t = std::unexpected<Error>;
	using error_type = Error;
	using error_t = Error;
	using base_t::base_t;

	constexpr unexpected(const unexpected&) = default;
	constexpr unexpected(unexpected&&) = default;

	constexpr unexpected &operator=(const unexpected&) = default;
	constexpr unexpected &operator=(unexpected&&) = default;
};

template <typename Error>
unexpected(Error) -> unexpected<Error>;

#else

struct unexpect_t {
	explicit constexpr unexpect_t() = default;
};
constexpr unexpect_t unexpect {};

template <typename Error>
class RIWO_CORE_TAPI unexpected
{
	static_assert (std::is_object_v<Error> and not std::is_array_v<Error>);

public:
	using error_type = Error;
	using error_t = Error;

	template <typename OtherError = error_type>
	constexpr explicit(not std::convertible_to<OtherError,error_type>)
	unexpected(OtherError &&error) requires (
		not std::same_as<std::remove_cvref_t<OtherError>,unexpected> and
		not std::same_as<std::remove_cvref_t<OtherError>,std::in_place_t> and
		std::constructible_from<error_type,OtherError>
	);

	template <typename...Args>
	constexpr explicit unexpected(std::in_place_t, Args&&...args)
		requires std::constructible_from<error_type,Args...>;

	template <typename U, typename...Args>
	constexpr explicit unexpected(std::in_place_t, std::initializer_list<U> list, Args&&...args)
		requires std::constructible_from<error_type,std::initializer_list<U>&,Args...>;

	constexpr unexpected(const unexpected&) = default;
	constexpr unexpected(unexpected&&) = default;

	constexpr unexpected &operator=(const unexpected&) = default;
	constexpr unexpected &operator=(unexpected&&) = default;

public:
	[[nodiscard]] constexpr const error_type &error() const & noexcept;
	[[nodiscard]] constexpr error_type &error() & noexcept;

	[[nodiscard]] constexpr const error_type &&error() const && noexcept;
	[[nodiscard]] constexpr error_type &&error() && noexcept;

	constexpr void swap(unexpected &other)
		noexcept(std::is_nothrow_swappable_v<error_type>)
		requires std::swappable<error_type>;

	friend constexpr bool operator==(const unexpected&, const unexpected&)
		requires std::equality_comparable<error_type> = default;

private:
	error_type m_error;
};

template <typename Error>
unexpected(Error) -> unexpected<Error>;

template <typename Error>
constexpr void swap(unexpected<Error> &left, unexpected<Error> &right)
	noexcept(noexcept(left.swap(right)));

template <typename Error>
class bad_expected_access;

#ifdef _MSC_VER
# pragma warning(push)
// Exporting the exception type keeps RTTI consistent across Riwo DLL boundaries.
// MSVC warns about its standard-library base even though all targets use /MD.
# pragma warning(disable: 4275)
#endif
template <>
class RIWO_CORE_API bad_expected_access<void> : public std::exception
{
public:
	using error_t = void;
	[[nodiscard]] const char *what() const noexcept override;

protected:
	bad_expected_access() noexcept = default;
	bad_expected_access(const bad_expected_access&) noexcept = default;
	bad_expected_access &operator=(const bad_expected_access&) noexcept = default;
	~bad_expected_access() override = default;
};
#ifdef _MSC_VER
# pragma warning(pop)
#endif

template <typename Error>
class RIWO_CORE_TAPI bad_expected_access : public bad_expected_access<void>
{
public:
	using error_t = Error;
	explicit bad_expected_access(Error error);

	[[nodiscard]] const Error &error() const & noexcept;
	[[nodiscard]] Error &error() & noexcept;

	[[nodiscard]] const Error &&error() const && noexcept;
	[[nodiscard]] Error &&error() && noexcept;

private:
	Error m_error;
};

#endif //RIWO_HAS_STD_EXPECTED

namespace concepts
{

template <typename Value>
concept expected_value = optional_value<Value> or std::is_void_v<Value>;

}} //namespace riwo::concepts

#if !RIWO_HAS_STD_EXPECTED
# include <riwo/core/cxx/detail/expected_polyfill.h>
#endif

namespace riwo
{

template <typename Value, typename Error>
class expected;

namespace detail
{

#if RIWO_HAS_STD_EXPECTED
template <typename Value, typename Error>
using expected_base_t = std::expected<Value,Error>;
#else
template <typename Value, typename Error>
using expected_base_t = expected_polyfill<Value,Error>;
#endif

template <typename T>
struct expected_traits;

template <typename Value, typename Error>
struct expected_traits<expected<Value,Error>>
{
	using value_type = Value;
	using error_type = Error;
};

#if RIWO_HAS_STD_EXPECTED
template <typename Value, typename Error>
struct expected_traits<std::expected<Value,Error>>
{
	using value_type = Value;
	using error_type = Error;
};
#endif

template <typename T>
concept expected_specialization = requires
{
	typename expected_traits<std::remove_cvref_t<T>>::value_type;
	typename expected_traits<std::remove_cvref_t<T>>::error_type;
};

template <typename T>
using expected_value_t = expected_traits<std::remove_cvref_t<T>>::value_type;

template <typename T>
using expected_error_t = expected_traits<std::remove_cvref_t<T>>::error_type;

template <typename Func, typename Self>
constexpr bool expected_invocable_v = []
{
	if constexpr( std::is_void_v<typename std::remove_cvref_t<Self>::value_type> )
		return std::invocable<Func>;
	else
		return std::invocable<Func,decltype(*std::declval<Self>())>;
}();

template <typename Func, typename Self>
concept expected_invocable = expected_invocable_v<Func,Self>;

template <typename Func, typename Self>
concept expected_or_else_invocable =
	std::invocable<Func,decltype(std::declval<Self>().error())> or
	std::invocable<Func>;

} //namespace detail

template <typename Value, typename Error>
class RIWO_CORE_TAPI expected final :
	public detail::expected_base_t<Value,Error>
{
public:
	using base_t = detail::expected_base_t<Value,Error>;
	using value_type = Value;
	using error_type = Error;

	using unexpected_type = unexpected<error_type>;
	using value_t = Value;
	using error_t = Error;

	template <typename U>
	using rebind = expected<U,error_type>;

public:
	using base_t::base_t;

	constexpr expected() requires (
		std::is_void_v<value_type> or std::default_initializable<value_type>
	) = default;

	constexpr expected(const expected&) = default;
	constexpr expected(expected&&) = default;

	constexpr expected &operator=(const expected&) = default;
	constexpr expected &operator=(expected&&) = default;

	constexpr expected(const base_t &other);
	constexpr expected(base_t &&other) noexcept (
		std::is_nothrow_move_constructible_v<base_t>
	);
	constexpr expected(const unexpected_type &error);
	constexpr expected(unexpected_type &&error);

	template <typename OtherError, typename U = value_type>
	constexpr expected(OtherError &&error) requires (
		not std::same_as<std::remove_cvref_t<OtherError>,error_type> and
		std::constructible_from<error_type,OtherError> and
		(std::is_void_v<U> or not std::convertible_to<OtherError,U>)
	);
	constexpr expected &operator=(const base_t &other);
	constexpr expected &operator=(base_t &&other) noexcept (
		std::is_nothrow_move_assignable_v<base_t>
	);
	constexpr expected &operator=(unexpected_type error);

	template <typename U = value_type>
	constexpr expected(std::type_identity_t<error_type> error)
		requires std::is_void_v<U> or (not std::convertible_to<error_type,U>);

	template <typename U = value_type>
	constexpr expected &operator=(U &&value) requires
		(not std::is_void_v<value_type>) and
		(not std::same_as<std::remove_cvref_t<U>,expected>) and
		(not std::same_as<std::remove_cvref_t<U>,base_t>) and
		std::constructible_from<value_type,U> and
		std::is_assignable_v<value_type&,U>;

public:
	[[nodiscard]] constexpr bool is_error() const noexcept;

	template <typename...Args>
	constexpr expected &despair(Args&&...args)
		requires std::constructible_from<error_type,Args...>;

	constexpr expected &despair(error_type error);
	constexpr expected &despair(unexpected_type error);

public:
	template <typename U>
	[[nodiscard]] constexpr value_type value_or(U &&fallback) const & requires
		(not std::is_void_v<value_type>) and
		std::copy_constructible<value_type> and
		std::convertible_to<U,value_type>;

	template <typename U>
	[[nodiscard]] constexpr value_type value_or(U &&fallback) && requires
		(not std::is_void_v<value_type>) and
		std::move_constructible<value_type> and
		std::convertible_to<U,value_type>;

	[[nodiscard]] constexpr value_type value_or() const & requires
		(not std::is_void_v<value_type>) and
		std::copy_constructible<value_type> and
		std::default_initializable<value_type>;

	[[nodiscard]] constexpr value_type value_or() && requires
		(not std::is_void_v<value_type>) and
		std::move_constructible<value_type> and
		std::default_initializable<value_type>;

public:
	template <typename Func>
	[[nodiscard]] constexpr auto and_then(Func &&func) &
		requires detail::expected_invocable<Func,expected&>;

	template <typename Func>
	[[nodiscard]] constexpr auto and_then(Func &&func) const &
		requires detail::expected_invocable<Func,const expected&>;

	template <typename Func>
	[[nodiscard]] constexpr auto and_then(Func &&func) &&
		requires detail::expected_invocable<Func,expected&&>;

	template <typename Func>
	[[nodiscard]] constexpr auto and_then(Func &&func) const &&
		requires detail::expected_invocable<Func,const expected&&>;

public:
	template <typename Func>
	[[nodiscard]] constexpr auto transform(Func &&func) &
		requires detail::expected_invocable<Func,expected&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform(Func &&func) const &
		requires detail::expected_invocable<Func,const expected&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform(Func &&func) &&
		requires detail::expected_invocable<Func,expected&&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform(Func &&func) const &&
		requires detail::expected_invocable<Func,const expected&&>;

public:
	template <typename Func>
	constexpr auto or_else(Func &&func) &
		requires detail::expected_or_else_invocable<Func,expected&>;

	template <typename Func>
	constexpr auto or_else(Func &&func) const &
		requires detail::expected_or_else_invocable<Func,const expected&>;

	template <typename Func>
	constexpr auto or_else(Func &&func) &&
		requires detail::expected_or_else_invocable<Func,expected&&>;

	template <typename Func>
	constexpr auto or_else(Func &&func) const &&
		requires detail::expected_or_else_invocable<Func,const expected&&>;

public:
	template <typename Func>
	[[nodiscard]] constexpr auto transform_error(Func &&func) &
		requires std::invocable<Func,error_type&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform_error(Func &&func) const &
		requires std::invocable<Func,const error_type&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform_error(Func &&func) &&
		requires std::invocable<Func,error_type&&>;

	template <typename Func>
	[[nodiscard]] constexpr auto transform_error(Func &&func) const &&
		requires std::invocable<Func,const error_type&&>;

public:
	template <typename U = value_type>
	[[nodiscard]] constexpr expected or_else(std::type_identity_t<U> value) const & requires
		std::same_as<U,value_type> and (not std::is_void_v<U>) and
		std::copy_constructible<value_type>;

	template <typename U = value_type>
	[[nodiscard]] constexpr expected or_else(std::type_identity_t<U> value) && requires
		std::same_as<U,value_type> and (not std::is_void_v<U>) and
		std::move_constructible<value_type>;

	[[nodiscard]] constexpr expected or_else() const & requires
		(not std::is_void_v<value_type>) and
		std::copy_constructible<value_type> and
		std::default_initializable<value_type>;

	[[nodiscard]] constexpr expected or_else() && requires
		(not std::is_void_v<value_type>) and
		std::move_constructible<value_type> and
		std::default_initializable<value_type>;

	[[nodiscard]] constexpr expected or_else() const &
		requires std::is_void_v<value_type> and std::copy_constructible<error_type>;

	[[nodiscard]] constexpr expected or_else() &&
		requires std::is_void_v<value_type> and std::move_constructible<error_type>;
};

template <typename Error, concepts::optional_value_p Value>
[[nodiscard]] constexpr auto make_expected(Value &&value);

template <typename Error>
[[nodiscard]] constexpr expected<void,Error> make_expected();

template <typename>
struct is_expected : std::false_type {};

template <typename Value, typename Error>
struct is_expected<expected<Value,Error>> : std::true_type {};

#if RIWO_HAS_STD_EXPECTED
template <typename Value, typename Error>
struct is_expected<std::expected<Value,Error>> : std::true_type {};
#endif

template <typename T>
constexpr bool is_expected_v = is_expected<T>::value;

namespace concepts
{

template <typename T>
concept expected = is_expected_v<T>;

template <typename T>
concept expected_p = expected<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts
#include <riwo/core/cxx/detail/expected.h>


#endif //RIWO_CORE_CXX_EXPECTED_H
