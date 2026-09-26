// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_EXPECTED_H
#define RIWO_CORE_CXX_DETAIL_EXPECTED_H

namespace riwo { namespace detail
{

template <typename Self, typename Func>
constexpr decltype(auto) invoke_expected(Self &&self, Func &&func)
{
	if constexpr( std::is_void_v<typename std::remove_cvref_t<Self>::value_type> )
		return std::invoke(std::forward<Func>(func));
	else
		return std::invoke(std::forward<Func>(func), *std::forward<Self>(self));
}

template <typename Self, typename Func>
constexpr decltype(auto) invoke_expected_error(Self &&self, Func &&func)
{
	if constexpr( std::invocable<Func,decltype(std::forward<Self>(self).error())> )
	{
		return std::invoke (
			std::forward<Func>(func), std::forward<Self>(self).error()
		);
	}
	else
		return std::invoke(std::forward<Func>(func));
}

template <typename Error, typename Self, typename Func>
constexpr auto expected_and_then(Self &&self, Func &&func)
{
	using result_t = std::remove_cvref_t<decltype(detail::invoke_expected (
		std::forward<Self>(self), std::forward<Func>(func)
	))>;
	static_assert(detail::expected_specialization<result_t>,
		"expected::and_then callback must return an expected specialization"
	);
	using result_error_t = expected_error_t<result_t>;

	static_assert(std::same_as<result_error_t,Error>,
		"expected::and_then callback must preserve the error type"
	);
	if( self.has_value() )
	{
		return detail::invoke_expected (
			std::forward<Self>(self), std::forward<Func>(func)
		);
	}
	return result_t(unexpected<result_error_t> (
		std::in_place, std::forward<Self>(self).error()
	));
}

template <typename Error, typename Self, typename Func>
constexpr auto expected_transform(Self &&self, Func &&func)
{
	using invoke_t = decltype(detail::invoke_expected (
		std::forward<Self>(self), std::forward<Func>(func)
	));
	using result_value_t = std::remove_cv_t<invoke_t>;

	if constexpr( std::is_void_v<result_value_t> )
	{
		if( self.has_value() )
		{
			detail::invoke_expected (
				std::forward<Self>(self), std::forward<Func>(func)
			);
			return expected<void,Error> {};
		}
		return expected<void,Error>(unexpected<Error> (
			std::in_place, std::forward<Self>(self).error()
		));
	}
	else
	{
		static_assert(std::is_object_v<result_value_t> and
			not std::is_array_v<result_value_t>
		);
		if( self.has_value() )
		{
			return expected<result_value_t,Error> (
				std::in_place, detail::invoke_expected (
					std::forward<Self>(self), std::forward<Func>(func)
				)
			);
		}
		return expected<result_value_t,Error>(unexpected<Error> (
			std::in_place, std::forward<Self>(self).error()
		));
	}
}

template <typename Value, typename Self, typename Func>
constexpr auto expected_or_else(Self &&self, Func &&func)
{
	using invoke_t = decltype(detail::invoke_expected_error (
		std::forward<Self>(self), std::forward<Func>(func)
	));
	if constexpr( std::is_void_v<invoke_t> )
	{
		if( not self.has_value() )
		{
			detail::invoke_expected_error (
				std::forward<Self>(self), std::forward<Func>(func)
			);
		}
		return expected(std::forward<Self>(self));
	}
	else
	{
		using result_t = std::remove_cvref_t<invoke_t>;
		static_assert(detail::expected_specialization<result_t>,
			"expected::or_else callback must return void or an expected specialization"
		);
		using result_value_t = expected_value_t<result_t>;

		static_assert(std::same_as<result_value_t,Value>,
			"expected::or_else callback must preserve the value type"
		);
		if( not self.has_value() )
		{
			return detail::invoke_expected_error (
				std::forward<Self>(self), std::forward<Func>(func)
			);
		}
		if constexpr( std::is_void_v<Value> )
			return result_t {};
		else
			return result_t(std::in_place, *std::forward<Self>(self));
	}
}

template <typename Value, typename Self, typename Func>
constexpr auto expected_transform_error(Self &&self, Func &&func)
{
	using result_error_t = std::remove_cv_t<std::invoke_result_t <
		Func, decltype(std::forward<Self>(self).error())
	>>;
	static_assert(std::is_object_v<result_error_t> and
		not std::is_array_v<result_error_t>
	);
	if( not self.has_value() )
	{
		return expected<Value,result_error_t>(unexpected<result_error_t> (
			std::in_place, std::invoke (
				std::forward<Func>(func), std::forward<Self>(self).error()
			)
		));
	}
	if constexpr( std::is_void_v<Value> )
		return expected<void,result_error_t> {};
	else
	{
		return expected<Value,result_error_t> (
			std::in_place, *std::forward<Self>(self)
		);
	}
}

} //namespace riwo::detail

#if !RIWO_HAS_STD_EXPECTED

template <typename Error>
template <typename OtherError>
constexpr unexpected<Error>::unexpected(OtherError &&error) requires (
	not std::same_as<std::remove_cvref_t<OtherError>,unexpected> and
	not std::same_as<std::remove_cvref_t<OtherError>,std::in_place_t> and
	std::constructible_from<error_type,OtherError>
) : m_error(std::forward<OtherError>(error))
{

}

template <typename Error>
template <typename...Args>
constexpr unexpected<Error>::unexpected(std::in_place_t, Args&&...args)
	requires std::constructible_from<error_type,Args...> :
	m_error(std::forward<Args>(args)...)
{

}

template <typename Error>
template <typename U, typename...Args>
constexpr unexpected<Error>::unexpected(std::in_place_t, std::initializer_list<U> list, Args&&...args)
	requires std::constructible_from<error_type,std::initializer_list<U>&,Args...> :
	m_error(list, std::forward<Args>(args)...)
{

}

template <typename Error>
constexpr auto unexpected<Error>::error() const & noexcept -> const error_type&
{
	return m_error;
}

template <typename Error>
constexpr auto unexpected<Error>::error() & noexcept -> error_type&
{
	return m_error;
}

template <typename Error>
constexpr auto unexpected<Error>::error() const && noexcept -> const error_type&&
{
	return std::move(m_error);
}

template <typename Error>
constexpr auto unexpected<Error>::error() && noexcept -> error_type&&
{
	return std::move(m_error);
}

template <typename Error>
constexpr void unexpected<Error>::swap(unexpected &other)
	noexcept(std::is_nothrow_swappable_v<error_type>)
	requires std::swappable<error_type>
{
	using std::swap;
	swap(m_error, other.m_error);
}

template <typename Error>
constexpr void swap(unexpected<Error> &left, unexpected<Error> &right)
	noexcept(noexcept(left.swap(right)))
{
	left.swap(right);
}

template <typename Error>
bad_expected_access<Error>::bad_expected_access(Error error) :
	m_error(std::move(error))
{

}

template <typename Error>
const Error &bad_expected_access<Error>::error() const & noexcept
{
	return m_error;
}

template <typename Error>
Error &bad_expected_access<Error>::error() & noexcept
{
	return m_error;
}

template <typename Error>
const Error &&bad_expected_access<Error>::error() const && noexcept
{
	return std::move(m_error);
}

template <typename Error>
Error &&bad_expected_access<Error>::error() && noexcept
{
	return std::move(m_error);
}

#endif //RIWO_HAS_STD_EXPECTED

template <typename Value, typename Error>
constexpr expected<Value,Error>::expected(const base_t &other) :
	base_t(other)
{

}

template <typename Value, typename Error>
constexpr expected<Value,Error>::expected(base_t &&other)
	noexcept(std::is_nothrow_move_constructible_v<base_t>) :
	base_t(std::move(other))
{

}

template <typename Value, typename Error>
constexpr expected<Value,Error>::expected(const unexpected_type &error) :
	base_t(unexpect, error.error())
{

}

template <typename Value, typename Error>
constexpr expected<Value,Error>::expected(unexpected_type &&error) :
	base_t(unexpect, std::move(error).error())
{

}

template <typename Value, typename Error>
template <typename OtherError, typename U>
constexpr expected<Value,Error>::expected(OtherError &&error) requires (
	not std::same_as<std::remove_cvref_t<OtherError>,error_type> and
	std::constructible_from<error_type,OtherError> and
	(std::is_void_v<U> or not std::convertible_to<OtherError,U>)
) : base_t(unexpect, std::forward<OtherError>(error))
{

}

template <typename Value, typename Error>
constexpr expected<Value,Error> &expected<Value,Error>::operator=(const base_t &other)
{
	base_t::operator=(other);
	return *this;
}

template <typename Value, typename Error>
constexpr expected<Value,Error> &expected<Value,Error>::operator=(base_t &&other)
	noexcept(std::is_nothrow_move_assignable_v<base_t>)
{
	base_t::operator=(std::move(other));
	return *this;
}

template <typename Value, typename Error>
constexpr expected<Value,Error> &expected<Value,Error>::operator=(unexpected_type error)
{
	base_t::operator=(typename base_t::unexpected_type (
		std::in_place, std::move(error).error()
	));
	return *this;
}

template <typename Value, typename Error>
template <typename U>
constexpr expected<Value,Error>::expected(std::type_identity_t<error_type> error)
	requires std::is_void_v<U> or (not std::convertible_to<error_type,U>) :
	base_t(unexpect, std::move(error))
{

}

template <typename Value, typename Error>
constexpr bool expected<Value,Error>::is_error() const noexcept
{
	return not this->has_value();
}

template <typename Value, typename Error>
template <typename U>
constexpr expected<Value,Error> &expected<Value,Error>::operator=(U &&value) requires
	(not std::is_void_v<value_type>) and
	(not std::same_as<std::remove_cvref_t<U>,expected>) and
	(not std::same_as<std::remove_cvref_t<U>,base_t>) and
	std::constructible_from<value_type,U> and
	std::is_assignable_v<value_type&,U>
{
	base_t::operator=(std::forward<U>(value));
	return *this;
}

template <typename Value, typename Error>
template <typename...Args>
constexpr expected<Value,Error> &expected<Value,Error>::despair(Args&&...args)
	requires std::constructible_from<error_type,Args...>
{
	return despair(unexpected_type(std::in_place, std::forward<Args>(args)...));
}

template <typename Value, typename Error>
constexpr expected<Value,Error> &expected<Value,Error>::despair(error_type error)
{
	return despair(unexpected_type(std::in_place, std::move(error)));
}

template <typename Value, typename Error>
constexpr expected<Value,Error> &expected<Value,Error>::despair(unexpected_type error)
{
	base_t::operator=(std::move(error));
	return *this;
}

template <typename Value, typename Error>
template <typename U>
constexpr Value expected<Value,Error>::value_or(U &&fallback) const & requires
	(not std::is_void_v<value_type>) and
	std::copy_constructible<value_type> and
	std::convertible_to<U,value_type>
{
	return base_t::value_or(std::forward<U>(fallback));
}

template <typename Value, typename Error>
template <typename U>
constexpr Value expected<Value,Error>::value_or(U &&fallback) && requires
	(not std::is_void_v<value_type>) and
	std::move_constructible<value_type> and
	std::convertible_to<U,value_type>
{
	return std::move(static_cast<base_t&>(*this)).value_or(std::forward<U>(fallback));
}

template <typename Value, typename Error>
constexpr Value expected<Value,Error>::value_or() const & requires
	(not std::is_void_v<value_type>) and
	std::copy_constructible<value_type> and
	std::default_initializable<value_type>
{
	return base_t::value_or(value_type {});
}

template <typename Value, typename Error>
constexpr Value expected<Value,Error>::value_or() && requires
	(not std::is_void_v<value_type>) and
	std::move_constructible<value_type> and
	std::default_initializable<value_type>
{
	return std::move(static_cast<base_t&>(*this)).value_or(value_type {});
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::and_then(Func &&func) &
	requires detail::expected_invocable<Func,expected&>
{
	return detail::expected_and_then<error_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::and_then(Func &&func) const &
	requires detail::expected_invocable<Func,const expected&>
{
	return detail::expected_and_then<error_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::and_then(Func &&func) &&
	requires detail::expected_invocable<Func,expected&&>
{
	return detail::expected_and_then<error_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::and_then(Func &&func) const &&
	requires detail::expected_invocable<Func,const expected&&>
{
	return detail::expected_and_then<error_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform(Func &&func) &
	requires detail::expected_invocable<Func,expected&>
{
	return detail::expected_transform<error_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform(Func &&func) const &
	requires detail::expected_invocable<Func,const expected&>
{
	return detail::expected_transform<error_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform(Func &&func) &&
	requires detail::expected_invocable<Func,expected&&>
{
	return detail::expected_transform<error_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform(Func &&func) const &&
	requires detail::expected_invocable<Func,const expected&&>
{
	return detail::expected_transform<error_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::or_else(Func &&func) &
	requires detail::expected_or_else_invocable<Func,expected&>
{
	return detail::expected_or_else<value_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::or_else(Func &&func) const &
	requires detail::expected_or_else_invocable<Func,const expected&>
{
	return detail::expected_or_else<value_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::or_else(Func &&func) &&
	requires detail::expected_or_else_invocable<Func,expected&&>
{
	return detail::expected_or_else<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::or_else(Func &&func) const &&
	requires detail::expected_or_else_invocable<Func,const expected&&>
{
	return detail::expected_or_else<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform_error(Func &&func) &
	requires std::invocable<Func,error_type&>
{
	return detail::expected_transform_error<value_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform_error(Func &&func) const &
	requires std::invocable<Func,const error_type&>
{
	return detail::expected_transform_error<value_t>(*this, std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform_error(Func &&func) &&
	requires std::invocable<Func,error_type&&>
{
	return detail::expected_transform_error<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename Func>
constexpr auto expected<Value,Error>::transform_error(Func &&func) const &&
	requires std::invocable<Func,const error_type&&>
{
	return detail::expected_transform_error<value_t>(std::move(*this), std::forward<Func>(func));
}

template <typename Value, typename Error>
template <typename U>
constexpr expected<Value,Error> expected<Value,Error>::or_else
(std::type_identity_t<U> value) const & requires
	std::same_as<U,value_type> and (not std::is_void_v<U>) and
	std::copy_constructible<value_type>
{
	return this->has_value() ? *this : expected(std::in_place, std::move(value));
}

template <typename Value, typename Error>
template <typename U>
constexpr expected<Value,Error> expected<Value,Error>::or_else
(std::type_identity_t<U> value) && requires
	std::same_as<U,value_type> and (not std::is_void_v<U>) and
	std::move_constructible<value_type>
{
	return this->has_value() ? std::move(*this) :
		expected(std::in_place, std::move(value));
}

template <typename Value, typename Error>
constexpr expected<Value,Error> expected<Value,Error>::or_else() const & requires
	(not std::is_void_v<value_type>) and std::copy_constructible<value_type> and
	std::default_initializable<value_type>
{
	return this->has_value() ? *this : expected(std::in_place);
}

template <typename Value, typename Error>
constexpr expected<Value,Error> expected<Value,Error>::or_else() && requires
	(not std::is_void_v<value_type>) and std::move_constructible<value_type> and
	std::default_initializable<value_type>
{
	return this->has_value() ? std::move(*this) : expected(std::in_place);
}

template <typename Value, typename Error>
constexpr expected<Value,Error> expected<Value,Error>::or_else() const &
	requires std::is_void_v<value_type> and std::copy_constructible<error_type>
{
	return this->has_value() ? *this : expected {};
}

template <typename Value, typename Error>
constexpr expected<Value,Error> expected<Value,Error>::or_else() &&
	requires std::is_void_v<value_type> and std::move_constructible<error_type>
{
	return this->has_value() ? std::move(*this) : expected {};
}

template <typename Error, concepts::optional_value_p Value>
constexpr auto make_expected(Value &&value)
{
	using value_type = std::remove_cvref_t<Value>;
	return expected<value_type,Error> (
		std::in_place, std::forward<Value>(value)
	);
}

template <typename Error>
constexpr expected<void,Error> make_expected()
{
	return expected<void,Error> {};
}

template <typename Value, typename Error>
constexpr void swap(expected<Value,Error> &left, expected<Value,Error> &right)
	noexcept(noexcept(left.swap(right)))
{
	left.swap(right);
}

template <typename Value, typename Error, typename OtherValue, typename OtherError>
[[nodiscard]] constexpr bool operator==
(const expected<Value,Error> &left, const expected<OtherValue,OtherError> &right)
	requires ((
		(std::is_void_v<Value> and std::is_void_v<OtherValue>) or (
				not std::is_void_v<Value> and
				not std::is_void_v<OtherValue> and
				requires { { *left == *right } -> std::convertible_to<bool>; }
			)
		) and
		requires { { left.error() == right.error() } -> std::convertible_to<bool>; }
	)
{
	if( left.has_value() != right.has_value() )
		return false;

	if( not left.has_value() )
		return left.error() == right.error();

	if constexpr( std::is_void_v<Value> )
		return true;
	else
		return *left == *right;
}

template <typename Value, typename Error, typename U>
[[nodiscard]] constexpr bool operator==
(const expected<Value,Error> &left, const U &right) requires
	(not std::is_void_v<Value>) and (not detail::expected_specialization<U>) and
	requires { { *left == right } -> std::convertible_to<bool>; }
{
	return left.has_value() and *left == right;
}

template <typename Value, typename Error, typename OtherError>
[[nodiscard]] constexpr bool operator==(const expected<Value,Error> &left, const unexpected<OtherError> &right)
	requires requires { { left.error() == right.error() } -> std::convertible_to<bool>; }
{
	return not left.has_value() and left.error() == right.error();
}

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_EXPECTED_H
