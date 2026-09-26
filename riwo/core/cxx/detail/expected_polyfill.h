// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_EXPECTED_POLYFILL_H
#define RIWO_CORE_CXX_DETAIL_EXPECTED_POLYFILL_H

#include <variant>

namespace riwo { namespace detail
{

template <typename Value, typename Error>
class RIWO_CORE_TAPI expected_polyfill
{
	static_assert(not std::is_void_v<Value>);
	static_assert(std::is_object_v<Value> and not std::is_array_v<Value>);
	static_assert(std::is_object_v<Error> and not std::is_array_v<Error>);

public:
	using value_type = Value;
	using error_type = Error;
	using unexpected_type = unexpected<error_type>;

	template <typename U>
	using rebind = expected_polyfill<U,error_type>;

	constexpr expected_polyfill()
		requires std::default_initializable<value_type> :
		m_storage(std::in_place_index<0>) {}

	constexpr expected_polyfill(const expected_polyfill&) = default;
	constexpr expected_polyfill(expected_polyfill&&) = default;

	constexpr expected_polyfill &operator=(const expected_polyfill&) = default;
	constexpr expected_polyfill &operator=(expected_polyfill&&) = default;
	~expected_polyfill() = default;

	template <typename U = value_type>
	constexpr explicit(not std::convertible_to<U,value_type>) expected_polyfill(U &&value) requires (
		not std::same_as<std::remove_cvref_t<U>,expected_polyfill> and
		not std::same_as<std::remove_cvref_t<U>,std::in_place_t> and
		not std::same_as<std::remove_cvref_t<U>,unexpect_t> and
		std::constructible_from<value_type,U>
	) :
	m_storage(std::in_place_index<0>, std::forward<U>(value)) {}

	template <typename OtherError>
	constexpr explicit(not std::convertible_to<const OtherError&,error_type>)
	expected_polyfill(const unexpected<OtherError> &error)
		requires std::constructible_from<error_type,const OtherError&> :
		m_storage(std::in_place_index<1>, error.error()) {}

	template <typename OtherError>
	constexpr explicit(not std::convertible_to<OtherError,error_type>)
	expected_polyfill(unexpected<OtherError> &&error)
		requires std::constructible_from<error_type,OtherError> :
		m_storage(std::in_place_index<1>, std::move(error).error()) {}

	template <typename...Args>
	constexpr explicit expected_polyfill(std::in_place_t, Args&&...args)
		requires std::constructible_from<value_type,Args...> :
		m_storage(std::in_place_index<0>, std::forward<Args>(args)...) {}

	template <typename U, typename...Args>
	constexpr explicit expected_polyfill
	(std::in_place_t, std::initializer_list<U> list, Args&&...args)
		requires std::constructible_from<value_type,std::initializer_list<U>&,Args...> :
		m_storage(std::in_place_index<0>, list, std::forward<Args>(args)...) {}

	template <typename...Args>
	constexpr explicit expected_polyfill(unexpect_t, Args&&...args)
		requires std::constructible_from<error_type,Args...> :
		m_storage(std::in_place_index<1>, std::forward<Args>(args)...) {}

	template <typename U, typename...Args>
	constexpr explicit expected_polyfill
	(unexpect_t, std::initializer_list<U> list, Args&&...args)
		requires std::constructible_from<error_type,std::initializer_list<U>&,Args...> :
		m_storage(std::in_place_index<1>, list, std::forward<Args>(args)...) {}

	template <typename OtherValue, typename OtherError>
	constexpr explicit (
		not std::convertible_to<const OtherValue&,value_type> or
		not std::convertible_to<const OtherError&,error_type>
	) expected_polyfill(const expected_polyfill<OtherValue,OtherError> &other) requires
		std::constructible_from<value_type,const OtherValue&> and
		std::constructible_from<error_type,const OtherError&> :
		m_storage(make_storage(other)) {}

	template <typename OtherValue, typename OtherError>
	constexpr explicit (
		not std::convertible_to<OtherValue,value_type> or
		not std::convertible_to<OtherError,error_type>
	) expected_polyfill(expected_polyfill<OtherValue,OtherError> &&other) requires
		std::constructible_from<value_type,OtherValue> and
		std::constructible_from<error_type,OtherError> :
		m_storage(make_storage(std::move(other))) {}

public:
	template <typename U = value_type>
	constexpr expected_polyfill &operator=(U &&value) requires (
		not std::same_as<std::remove_cvref_t<U>,expected_polyfill> and
		std::constructible_from<value_type,U>
	){
		m_storage.template emplace<0>(std::forward<U>(value));
		return *this;
	}

	template <typename OtherError>
	constexpr expected_polyfill &operator=(const unexpected<OtherError> &error)
		requires std::constructible_from<error_type,const OtherError&>
	{
		m_storage.template emplace<1>(error.error());
		return *this;
	}

	template <typename OtherError>
	constexpr expected_polyfill &operator=(unexpected<OtherError> &&error)
		requires std::constructible_from<error_type,OtherError>
	{
		m_storage.template emplace<1>(std::move(error).error());
		return *this;
	}

	template <typename...Args>
	constexpr value_type &emplace(Args&&...args)
		requires std::constructible_from<value_type,Args...>
	{
		return m_storage.template emplace<0>(std::forward<Args>(args)...);
	}

	template <typename U, typename...Args>
	constexpr value_type &emplace(std::initializer_list<U> list, Args&&...args)
		requires std::constructible_from<value_type,std::initializer_list<U>&,Args...>
	{
		return m_storage.template emplace<0>(list, std::forward<Args>(args)...);
	}

	constexpr void swap(expected_polyfill &other)
		noexcept(noexcept(m_storage.swap(other.m_storage)))
		requires std::swappable<value_type> and std::swappable<error_type>
	{
		m_storage.swap(other.m_storage);
	}

public:
	[[nodiscard]] constexpr const value_type *operator->() const noexcept {
		return std::addressof(std::get<0>(m_storage));
	}

	[[nodiscard]] constexpr value_type *operator->() noexcept {
		return std::addressof(std::get<0>(m_storage));
	}

	[[nodiscard]] constexpr const value_type &operator*() const & noexcept {
		return std::get<0>(m_storage);
	}

	[[nodiscard]] constexpr value_type &operator*() & noexcept {
		return std::get<0>(m_storage);
	}

	[[nodiscard]] constexpr const value_type &&operator*() const && noexcept {
		return std::move(std::get<0>(m_storage));
	}

	[[nodiscard]] constexpr value_type &&operator*() && noexcept {
		return std::move(std::get<0>(m_storage));
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return has_value();
	}

	[[nodiscard]] constexpr bool has_value() const noexcept {
		return m_storage.index() == 0;
	}

	[[nodiscard]] constexpr const value_type &value() const &
		requires std::copy_constructible<error_type>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(error());
		return **this;
	}

	[[nodiscard]] constexpr value_type &value() &
		requires std::copy_constructible<error_type>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(error());
		return **this;
	}

	[[nodiscard]] constexpr const value_type &&value() const &&
		requires std::constructible_from<error_type,const error_type&&>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(std::move(*this).error());
		return *std::move(*this);
	}

	[[nodiscard]] constexpr value_type &&value() &&
		requires std::move_constructible<error_type>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(std::move(*this).error());
		return *std::move(*this);
	}

	[[nodiscard]] constexpr const error_type &error() const & noexcept {
		return std::get<1>(m_storage);
	}

	[[nodiscard]] constexpr error_type &error() & noexcept {
		return std::get<1>(m_storage);
	}

	[[nodiscard]] constexpr const error_type &&error() const && noexcept {
		return std::move(std::get<1>(m_storage));
	}

	[[nodiscard]] constexpr error_type &&error() && noexcept {
		return std::move(std::get<1>(m_storage));
	}

public:
	template <typename U>
	[[nodiscard]] constexpr value_type value_or(U &&fallback) const &
		requires std::copy_constructible<value_type> and std::convertible_to<U,value_type>
	{
		return has_value() ? **this : static_cast<value_type>(std::forward<U>(fallback));
	}

	template <typename U>
	[[nodiscard]] constexpr value_type value_or(U &&fallback) &&
		requires std::move_constructible<value_type> and std::convertible_to<U,value_type>
	{
		return has_value() ? *std::move(*this) :
			static_cast<value_type>(std::forward<U>(fallback));
	}

	template <typename G>
	[[nodiscard]] constexpr error_type error_or(G &&fallback) const &
		requires std::copy_constructible<error_type> and std::convertible_to<G,error_type>
	{
		return has_value() ? static_cast<error_type>(std::forward<G>(fallback)) : error();
	}

	template <typename G>
	[[nodiscard]] constexpr error_type error_or(G &&fallback) &&
		requires std::move_constructible<error_type> and std::convertible_to<G,error_type>
	{
		return has_value() ? static_cast<error_type>(std::forward<G>(fallback)) :
			std::move(*this).error();
	}

private:
	using storage_t = std::variant<value_type,error_type>;

	template <typename OtherValue, typename OtherError>
	[[nodiscard]] static constexpr storage_t make_storage
	(const expected_polyfill<OtherValue,OtherError> &other)
	{
		return other.has_value() ? storage_t(std::in_place_index<0>, *other) :
			storage_t(std::in_place_index<1>, other.error());
	}

	template <typename OtherValue, typename OtherError>
	[[nodiscard]] static constexpr storage_t make_storage
	(expected_polyfill<OtherValue,OtherError> &&other)
	{
		return other.has_value() ?
			storage_t(std::in_place_index<0>, *std::move(other)) :
			storage_t(std::in_place_index<1>, std::move(other).error());
	}

	storage_t m_storage;
};

template <typename Error>
class RIWO_CORE_TAPI expected_polyfill<void,Error>
{
	static_assert(std::is_object_v<Error> and not std::is_array_v<Error>);

public:
	using value_type = void;
	using error_type = Error;
	using unexpected_type = unexpected<error_type>;

	template <typename U>
	using rebind = expected_polyfill<U,error_type>;

	constexpr expected_polyfill() noexcept :
		m_storage(std::in_place_index<0>) {}

	constexpr expected_polyfill(const expected_polyfill&) = default;
	constexpr expected_polyfill(expected_polyfill&&) = default;

	constexpr expected_polyfill &operator=(const expected_polyfill&) = default;
	constexpr expected_polyfill &operator=(expected_polyfill&&) = default;
	~expected_polyfill() = default;

	template <typename OtherError>
	constexpr explicit(not std::convertible_to<const OtherError&,error_type>)
	expected_polyfill(const unexpected<OtherError> &error)
		requires std::constructible_from<error_type,const OtherError&> :
		m_storage(std::in_place_index<1>, error.error()) {}

	template <typename OtherError>
	constexpr explicit(not std::convertible_to<OtherError,error_type>)
	expected_polyfill(unexpected<OtherError> &&error)
		requires std::constructible_from<error_type,OtherError> :
		m_storage(std::in_place_index<1>, std::move(error).error()) {}

	constexpr explicit expected_polyfill(std::in_place_t) noexcept :
		m_storage(std::in_place_index<0>) {}

	template <typename...Args>
	constexpr explicit expected_polyfill(unexpect_t, Args&&...args)
		requires std::constructible_from<error_type,Args...> :
		m_storage(std::in_place_index<1>, std::forward<Args>(args)...) {}

	template <typename U, typename...Args>
	constexpr explicit expected_polyfill(unexpect_t, std::initializer_list<U> list, Args&&...args)
		requires std::constructible_from<error_type,std::initializer_list<U>&,Args...> :
		m_storage(std::in_place_index<1>, list, std::forward<Args>(args)...) {}

public:
	template <typename OtherError>
	constexpr expected_polyfill &operator=(const unexpected<OtherError> &error)
		requires std::constructible_from<error_type,const OtherError&>
	{
		m_storage.template emplace<1>(error.error());
		return *this;
	}

	template <typename OtherError>
	constexpr expected_polyfill &operator=(unexpected<OtherError> &&error)
		requires std::constructible_from<error_type,OtherError>
	{
		m_storage.template emplace<1>(std::move(error).error());
		return *this;
	}

	constexpr void emplace() noexcept {
		m_storage.template emplace<0>();
	}

	constexpr void swap(expected_polyfill &other)
		noexcept(noexcept(m_storage.swap(other.m_storage)))
		requires std::swappable<error_type>
	{
		m_storage.swap(other.m_storage);
	}

	constexpr void operator*() const noexcept {}

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return has_value();
	}

	[[nodiscard]] constexpr bool has_value() const noexcept {
		return m_storage.index() == 0;
	}

	constexpr void value() const &
		requires std::copy_constructible<error_type>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(error());
	}

	constexpr void value() &&
		requires std::move_constructible<error_type>
	{
		if( not has_value() )
			throw bad_expected_access<error_type>(std::move(*this).error());
	}

	[[nodiscard]] constexpr const error_type &error() const & noexcept {
		return std::get<1>(m_storage);
	}

	[[nodiscard]] constexpr error_type &error() & noexcept {
		return std::get<1>(m_storage);
	}

	[[nodiscard]] constexpr const error_type &&error() const && noexcept {
		return std::move(std::get<1>(m_storage));
	}

	[[nodiscard]] constexpr error_type &&error() && noexcept {
		return std::move(std::get<1>(m_storage));
	}

public:
	template <typename G>
	[[nodiscard]] constexpr error_type error_or(G &&fallback) const &
		requires std::copy_constructible<error_type> and std::convertible_to<G,error_type>
	{
		return has_value() ? static_cast<error_type>(std::forward<G>(fallback)) : error();
	}

	template <typename G>
	[[nodiscard]] constexpr error_type error_or(G &&fallback) &&
		requires std::move_constructible<error_type> and std::convertible_to<G,error_type>
	{
		return has_value() ? static_cast<error_type>(std::forward<G>(fallback)) :
			std::move(*this).error();
	}

private:
	std::variant<std::monostate,error_type> m_storage;
};

}} //namespace riwo::detail


#endif //RIWO_CORE_CXX_DETAIL_EXPECTED_POLYFILL_H
