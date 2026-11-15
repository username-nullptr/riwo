// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_DETAIL_AS_EXPECTED_H
#define RIWO_CORE_OPERATION_DETAIL_AS_EXPECTED_H

#include <riwo/core/expected.h>

namespace riwo { namespace detail
{

template <typename...Args>
struct as_expected_value;

template <>
struct as_expected_value<> {
	using type = void;
};

template <typename Arg>
struct as_expected_value<Arg> {
	using type = std::decay_t<Arg>;
};

template <typename First, typename Second, typename...Rest>
struct as_expected_value<First,Second,Rest...>
{
	using type = std::tuple <
		std::decay_t<First>, std::decay_t<Second>, std::decay_t<Rest>...
	>;
};

template <typename...Args>
using as_expected_result_t = expected <
	typename as_expected_value<Args...>::type, error_code
>;

template <typename>
struct as_expected_signature {
	static constexpr bool valid = false;
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...)>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>);
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...) &>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>) &;
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...) &&>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>) &&;
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...) noexcept>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>) noexcept;
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...) & noexcept>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>) & noexcept;
};

template <typename Return, typename Error, typename...Args>
	requires std::same_as<std::remove_cvref_t<Error>,error_code>
struct as_expected_signature<Return(Error,Args...) && noexcept>
{
	static constexpr bool valid = true;
	using type = Return(as_expected_result_t<Args...>) && noexcept;
};

template <typename Handler>
class RIWO_CORE_TAPI as_expected_handler
{
public:
	template <typename RedirectedHandler>
	explicit as_expected_handler(RedirectedHandler &&handler) requires
		(not std::same_as<std::remove_cvref_t<RedirectedHandler>, as_expected_handler>):
		m_handler(std::forward<RedirectedHandler>(handler)) {}

	template <typename Error, typename...Args>
	void operator()(Error &&error, Args&&...args) requires
		std::same_as<std::remove_cvref_t<Error>,error_code>
	{
		using result_t = as_expected_result_t<Args...>;
		if( error )
		{
			static_cast<Handler&&>(m_handler) (
				result_t(unexpected(error_code(error)))
			);
		}
		else if constexpr( sizeof...(Args) == 0 )
			static_cast<Handler&&>(m_handler)(result_t {});
		else if constexpr( sizeof...(Args) == 1 )
		{
			static_cast<Handler&&>(m_handler) (
				result_t(std::forward<Args>(args)...)
			);
		}
		else
		{
			using value_t = result_t::value_type;
			static_cast<Handler&&>(m_handler) (
				result_t(value_t(std::forward<Args>(args)...))
			);
		}
	}

	Handler m_handler;
};

} //namespace detail

template <concepts::not_as_tuple_p Token>
template <concepts::not_as_tuple_p CompletionToken>
constexpr as_expected_t<Token>::as_expected_t(CompletionToken &&completion_token) :
	token(std::forward<CompletionToken>(completion_token))
{

}

template <concepts::not_as_tuple_p Token>
constexpr as_expected_t<Token>::as_expected_t(default_constructor_tag, token_t completion_token) :
	token(std::move(completion_token))
{

}

template <concepts::not_as_tuple_p Token>
template <typename Object>
auto as_expected_t<Token>::as_default_on(Object &&object) -> as_default_on_t<Object>
{
	return as_default_on_t<Object>(std::forward<Object>(object));
}

template <concepts::not_as_tuple_p CompletionToken>
constexpr auto partial_as_expected::operator()(CompletionToken &&completion_token) const
{
	return as_expected_t<std::decay_t<CompletionToken>>(
		std::forward<CompletionToken>(completion_token)
	);
}

} //namespace riwo::detail

#if RIWO_USING_BOOST_ASIO
namespace boost::asio
#else //RIWO_USING_BOOST_ASIO
namespace asio
#endif //RIWO_USING_BOOST_ASIO
{

template <typename CompletionToken, typename...Signatures>
	requires ((riwo::detail::as_expected_signature<Signatures>::valid) and ...)
class async_result<riwo::as_expected_t<CompletionToken>, Signatures...> :
	public async_result<CompletionToken,
		typename riwo::detail::as_expected_signature<Signatures>::type...
	>
{
	template <typename Initiation>
	class init_wrapper : public detail::initiation_base<Initiation>
	{
		using base_t = detail::initiation_base<Initiation>;

	public:
		using base_t::base_t;

		template <typename Handler, typename...Args>
		void operator()(Handler &&handler, Args&&...args) &&
		{
			static_cast<Initiation&&>(*this) (
				riwo::detail::as_expected_handler<std::decay_t<Handler>>(
					std::forward<Handler>(handler)
				),
				std::forward<Args>(args)...
			);
		}

		template <typename Handler, typename...Args>
		void operator()(Handler &&handler, Args&&...args) const &
		{
			static_cast<const Initiation&>(*this) (
				riwo::detail::as_expected_handler<std::decay_t<Handler>>(
					std::forward<Handler>(handler)
				),
				std::forward<Args>(args)...
			);
		}
	};

public:
	template <typename Initiation, typename RawCompletionToken, typename...Args>
	static auto initiate(Initiation &&initiation, RawCompletionToken &&token, Args&&...args)
	{
		using token_t = std::conditional_t <
			std::is_const_v<std::remove_reference_t<RawCompletionToken>>,
			const CompletionToken, CompletionToken
		>;
		return async_initiate
		<token_t, typename riwo::detail::as_expected_signature<Signatures>::type...>(
			init_wrapper<std::decay_t<Initiation>>(
				std::forward<Initiation>(initiation)
			),
			token.token, std::forward<Args>(args)...
		);
	}
};

template <template <typename,typename> class Associator, typename Handler, typename DefaultCandidate>
struct associator<Associator, riwo::detail::as_expected_handler<Handler>, DefaultCandidate> :
	Associator<Handler,DefaultCandidate>
{
	using base_t = Associator<Handler,DefaultCandidate>;
	using type = base_t::type;

	static type get(const riwo::detail::as_expected_handler<Handler> &handler) noexcept {
		return base_t::get(handler.m_handler);
	}

	static type get(const riwo::detail::as_expected_handler<Handler> &handler,
		const DefaultCandidate &candidate) noexcept {
		return base_t::get(handler.m_handler, candidate);
	}
};

template <typename...Signatures>
	requires ((riwo::detail::as_expected_signature<Signatures>::valid) and ...)
class async_result<riwo::partial_as_expected, Signatures...>
{
	template <typename Initiation, typename RawCompletionToken, typename...Args>
	static auto initiate(Initiation &&initiation, RawCompletionToken &&, Args&&...args)
	{
		using default_token_t = default_completion_token_t <
			associated_executor_t<Initiation>
		>;
		static_assert(riwo::concepts::not_as_tuple_p<default_token_t>,
			"riwo::as_expected and asio::as_tuple cannot be combined"
		);
		return async_initiate<Signatures...>(
			std::forward<Initiation>(initiation),
			riwo::as_expected_t<default_token_t> {},
			std::forward<Args>(args)...
		);
	}
};

} //namespace boost::asio or asio


#endif //RIWO_CORE_OPERATION_DETAIL_AS_EXPECTED_H
