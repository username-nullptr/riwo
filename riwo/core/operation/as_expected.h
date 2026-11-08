// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_AS_EXPECTED_H
#define RIWO_CORE_OPERATION_AS_EXPECTED_H

#include <riwo/core/operation/token_concepts.h>

namespace riwo { namespace concepts
{

template <typename T>
concept not_as_tuple_p = not as_tuple_p<T>;

} //namespace concepts

template <concepts::not_as_tuple_p Token>
class RIWO_CORE_TAPI as_expected_t
{
public:
	using token_t = Token;
	using token_type = token_t;
	struct default_constructor_tag {};

public:
	template <concepts::not_as_tuple_p CompletionToken>
	constexpr explicit as_expected_t(CompletionToken &&completion_token);

	constexpr as_expected_t (
		default_constructor_tag = default_constructor_tag(),
		token_t completion_token = token_t()
	);

public:
	template <typename InnerExecutor>
	struct executor_with_default : InnerExecutor
	{
		using default_completion_token_type = as_expected_t;
		using default_completion_token_t = default_completion_token_type;

		template <typename OtherExecutor>
		explicit executor_with_default(const OtherExecutor &executor) noexcept requires (
			not std::same_as<OtherExecutor,executor_with_default> and
			std::convertible_to<OtherExecutor,InnerExecutor>
		) :
		InnerExecutor(executor) {}
	};

	template <typename Object>
	using as_default_on_t = std::decay_t<Object>::template rebind_executor <
		executor_with_default<typename std::decay_t<Object>::executor_type>
	>::other;

	template <typename Object>
	[[nodiscard]] static as_default_on_t<Object> as_default_on(Object &&object);
	token_t token;
};

struct RIWO_CORE_VAPI partial_as_expected
{
	template <concepts::not_as_tuple_p CompletionToken>
	[[nodiscard]] constexpr auto operator()(CompletionToken &&completion_token) const;
};

constexpr partial_as_expected as_expected;

template <typename>
struct is_as_expected : std::false_type {};

template <typename T>
struct is_as_expected<as_expected_t<T>> : std::true_type {};

template <typename T>
constexpr bool is_as_expected_v = is_as_expected<T>::value;

template <typename Token>
struct is_use_sync<as_expected_t<Token>> : is_use_sync<Token> {};

namespace concepts
{

template <typename T>
concept as_expected = is_as_expected_v<T>;

template <typename T>
concept as_expected_p = as_expected<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts
#include <riwo/core/operation/detail/as_expected.h>


#endif //RIWO_CORE_OPERATION_AS_EXPECTED_H
