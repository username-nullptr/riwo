// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_OPT_TOKEN_H
#define RIWO_CORE_UTILS_OPT_TOKEN_H

#include <riwo/core/utils/asio_concepts.h>
#include <riwo/core/cxx/type_traits.h>
#include <riwo/core/cxx/attributes.h>

#if RIWO_USING_BOOST_ASIO
# include <boost/asio/experimental/awaitable_operators.hpp>
#else //RIWO_USING_BOOST_ASIO
# include <asio/experimental/awaitable_operators.hpp>
#endif //RIWO_USING_BOOST_ASIO

namespace riwo { namespace detail
{

template <typename CompletionToken>
class RIWO_CORE_TAPI std_error_token_t
{
public:
	using token_type = CompletionToken;
	constexpr std_error_token_t() = default;

	constexpr explicit std_error_token_t(CompletionToken token) :
		token(std::move(token)) {}

	template <typename OtherAllocator>
	[[nodiscard]] constexpr auto rebind(const OtherAllocator &allocator) const
		requires requires(const CompletionToken &value) { value.rebind(allocator); }
	{
		return std_error_token_t<decltype(token.rebind(allocator))>(
			token.rebind(allocator)
		);
	}

	[[nodiscard]] constexpr auto get_allocator() const
		requires requires(const CompletionToken &value) { value.get_allocator(); } {
		return token.get_allocator();
	}

	template <typename InnerExecutor>
	struct executor_with_default : InnerExecutor
	{
		using default_completion_token_type = std_error_token_t;

		template <typename OtherExecutor>
		explicit executor_with_default(const OtherExecutor &executor) noexcept
			requires (
				not std::same_as<OtherExecutor,executor_with_default> and
				std::convertible_to<OtherExecutor,InnerExecutor>
			) : InnerExecutor(executor) {}
	};

	template <typename Object>
	using as_default_on_t = Object::template rebind_executor <
		executor_with_default<typename Object::executor_type>
	>::other;

	template <typename Object>
	[[nodiscard]] static as_default_on_t<std::decay_t<Object>>
	as_default_on(Object &&object)
	{
		return as_default_on_t<std::decay_t<Object>>(
			std::forward<Object>(object)
		);
	}
	CompletionToken token {};
};

template <typename Allocator>
class RIWO_CORE_TAPI std_error_token_t<asio::use_future_t<Allocator>>
{
public:
	using token_type = asio::use_future_t<Allocator>;
	using allocator_type = Allocator;

	constexpr std_error_token_t() = default;
	constexpr explicit std_error_token_t(token_type token) :
		token(std::move(token)) {}

	template <typename OtherAllocator>
	[[nodiscard]] auto operator[](const OtherAllocator &allocator) const {
		return rebind(allocator);
	}

	template <typename OtherAllocator>
	[[nodiscard]] auto rebind(const OtherAllocator &allocator) const
	{
		return std_error_token_t<asio::use_future_t<OtherAllocator>>(
			token.rebind(allocator));
	}

	[[nodiscard]] allocator_type get_allocator() const {
		return token.get_allocator();
	}

	template <typename Function>
	[[nodiscard]] auto operator()(Function &&function) const {
		return token(std::forward<Function>(function));
	}
	token_type token {};
};

template <typename Handler>
class std_error_handler
{
public:
	explicit std_error_handler(Handler handler) :
		handler(std::move(handler)) {}

	void operator()() {
		std::move(handler)();
	}

	template <typename First, typename...Rest>
	void operator()(First &&first, Rest&&...rest)
	{
		using first_t = std::remove_cvref_t<First>;
		if constexpr( std::same_as<first_t,error_code> or std::same_as<first_t,std::error_code> )
		{
			std::exception_ptr exception;
			if( first )
			{
				exception = std::make_exception_ptr (
					std::system_error(std::error_code(first))
				);
			}
			std::move(handler)(std::move(exception),
				std::forward<Rest>(rest)...
			);
		}
		else
		{
			std::move(handler)(std::forward<First>(first),
				std::forward<Rest>(rest)...
			);
		}
	}
	Handler handler;
};

template <typename Signature>
struct std_error_signature {
	using type = Signature;
};

template <typename Return, typename...Args>
struct std_error_signature<Return(error_code,Args...)> {
	using type = Return(std::exception_ptr,Args...);
};

} //namespace detail

template <concepts::exec Exec = asio::any_io_executor>
using use_basic_awaitable_t = detail::std_error_token_t <
	asio::use_awaitable_t<Exec>
>;

using use_awaitable_t = use_basic_awaitable_t<asio::any_io_executor>;
constexpr use_awaitable_t use_awaitable(asio::use_awaitable);

template <typename Allocator = std::allocator<void>>
using use_basic_future_t = detail::std_error_token_t <
	asio::use_future_t<Allocator>
>;

using use_future_t = use_basic_future_t<std::allocator<void>>;
constexpr use_future_t use_future(asio::use_future);

using detached_t = asio::detached_t;
constexpr auto detached = asio::detached;

using deferred_t = asio::deferred_t;
constexpr auto deferred = asio::deferred;

struct use_sync_t {};
constexpr use_sync_t use_sync;

template <typename Token>
using redirect_error_t = asio::redirect_error_t<Token>;

template <typename Token, typename CancellationSlot>
using cancellation_slot_binder = asio::cancellation_slot_binder<Token, CancellationSlot>;

template <typename Token>
class RIWO_CORE_TAPI redirect_time_t
{
public:
	using token_t = Token;

	template <typename Rep, typename Period>
	redirect_time_t(auto &&completion_token, const duration<Rep,Period> &rtime);

	template <typename Clock, typename Duration>
	redirect_time_t(auto &&completion_token, const time_point<Clock,Duration> &atime);

	token_t token;
	milliseconds time {0};
};

template <typename Token, typename Rep, typename Period>
[[nodiscard]] RIWO_CORE_TAPI auto redirect_time (
	Token &&token, const duration<Rep,Period> &timeout
);

template <typename Token, typename Clock, typename Duration>
[[nodiscard]] RIWO_CORE_TAPI auto redirect_time (
	Token &&token, const time_point<Clock,Duration> &timeout
);

} //namespace riwo

#if RIWO_USING_BOOST_ASIO
namespace boost::asio
#else //RIWO_USING_BOOST_ASIO
namespace asio
#endif //RIWO_USING_BOOST_ASIO
{

template <typename CompletionToken, typename...Signatures>
class async_result<riwo::detail::std_error_token_t<CompletionToken>, Signatures...> :
	public async_result<CompletionToken,
		typename riwo::detail::std_error_signature<Signatures>::type...
	>
{
	template <typename Initiation>
	class init_wrapper
	{
	public:
		explicit init_wrapper(Initiation initiation) :
			m_initiation(std::move(initiation)) {}

		template <typename Handler, typename...Args>
		void operator()(Handler &&handler, Args&&...args)
		{
			if constexpr( (std::same_as<Signatures,
				typename riwo::detail::std_error_signature<Signatures>::type> and ...) )
			{
				std::move(m_initiation)(std::forward<Handler>(handler),
					std::forward<Args>(args)...
				);
			}
			else
			{
				std::move(m_initiation) (
					riwo::detail::std_error_handler
						<std::decay_t<Handler>>(std::forward<Handler>(handler)),
					std::forward<Args>(args)...
				);
			}
		}

	private:
		Initiation m_initiation;
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
		<token_t, typename riwo::detail::std_error_signature<Signatures>::type...>(
			init_wrapper<std::decay_t<Initiation>>(std::forward<Initiation>(initiation)),
			token.token, std::forward<Args>(args)...
		);
	}
};

template <template <typename,typename> class Associator, typename Handler, typename DefaultCandidate>
struct associator<Associator, riwo::detail::std_error_handler<Handler>, DefaultCandidate> :
	Associator<Handler,DefaultCandidate>
{
	using base_t = Associator<Handler,DefaultCandidate>;
	using type = base_t::type;

	static type get(const riwo::detail::std_error_handler<Handler> &handler) noexcept {
		return base_t::get(handler.handler);
	}
	static type get
	(const riwo::detail::std_error_handler<Handler> &handler, const DefaultCandidate &candidate) noexcept {
		return base_t::get(handler.handler, candidate);
	}
};

} //namespace boost::asio or asio
#include <riwo/core/utils/detail/opt_token.h>


#endif //RIWO_CORE_UTILS_OPT_TOKEN_H
