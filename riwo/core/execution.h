// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_EXECUTION_H
#define RIWO_CORE_EXECUTION_H

#include <riwo/core/jthread.h>

namespace riwo
{

using io_context_t = asio::io_context;
using io_executor_t = io_context_t::executor_type;

[[nodiscard]] RIWO_CORE_API io_context_t &io_context() noexcept;
[[nodiscard]] RIWO_CORE_API io_executor_t get_executor() noexcept;
/*
 * Start event scheduling;
 * This function will block until it returns after calling
 * exit or terminate.
 */
RIWO_CORE_API int exec();

RIWO_CORE_API void exec(io_context_t &ioc);
RIWO_CORE_API void exec_detach(io_context_t &ioc);

// End event scheduling;
RIWO_CORE_API void exit(int code = 0);

[[nodiscard]] RIWO_CORE_API bool is_run();

namespace concepts
{

template <typename Work>
concept dispatch_work = callable<Work> or awaitable_p<Work&&>;

template <typename Token, typename Work>
concept dispatch_token = []() consteval -> bool
{
	if constexpr( dispatch_work<Work> )
	{
		using work_t = std::remove_cvref_t<Work>;
		if constexpr( is_awaitable_v<work_t> )
		{
			using return_t = work_t::value_type;
			if constexpr( std::is_void_v<return_t> )
				return opt_token<Token, std::exception_ptr>;
			else
				return opt_token<Token, std::exception_ptr, return_t>;
		}
		else if constexpr( callable<Work> )
		{
			using return_t = std::invoke_result_t<Work>;
			if constexpr( is_awaitable_v<return_t> )
			{
				using nreturn_t = return_t::value_type;
				if constexpr( std::is_void_v<nreturn_t> )
					return opt_token<Token, std::exception_ptr>;
				else
					return opt_token<Token, std::exception_ptr, nreturn_t>;
			}
			else if constexpr( std::is_void_v<return_t> )
				return opt_token<Token, std::exception_ptr>;
			else
				return opt_token<Token, std::exception_ptr, return_t>;
		}
		else
			return false;
	}
	return false;
}();

} //namespace concepts
/*
 * If the current context is consistent with the executor context,
 * the work is executed immediately, otherwise it is pushed to the work queue.
 */
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token = const detached_t&>
RIWO_CORE_TAPI decltype(auto) dispatch (
	concepts::sched auto &&exec, Work &&work, Token &&token = detached
);
/*
 * If the current context is consistent with main executor context [riwo::io_context()],
 * the work is executed immediately, otherwise it is pushed to the work queue.
 */
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token = const detached_t&>
RIWO_CORE_TAPI decltype(auto) dispatch (
	Work &&work, Token &&token = detached
);

// Push a work to a work queue.
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token = const detached_t&>
RIWO_CORE_TAPI decltype(auto) post (
	concepts::sched auto &&exec, Work &&work, Token &&token = detached
);

// Push a work to a work queue.
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token = const detached_t&>
RIWO_CORE_TAPI decltype(auto) post (
	Work &&work, Token &&token = detached
);

using work_canceller_t = std::function<void()>;
/*
 * Push a work to a work queue.
 * The work will be executed after the specified relative time.
 */
template <concepts::dispatch_work Work, typename Rep, typename Period>
RIWO_CORE_TAPI work_canceller_t post (
	concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Work &&work
);
/*
 * Push a work to a work queue.
 * The work will be executed after the specified relative time.
 */
template <concepts::dispatch_work Work, typename Rep, typename Period>
RIWO_CORE_TAPI work_canceller_t post (
	const duration<Rep,Period> &rtime, Work &&work
);
/*
 * Push a work to a work queue.
 * The work will be executed at the specified absolute time.
 */
template <concepts::dispatch_work Work, typename Clock, typename Duration>
RIWO_CORE_TAPI work_canceller_t post (
	concepts::sched auto &&exec, const time_point<Clock,Duration> &atime, Work &&work
);
/*
 * Push a work to a work queue.
 * The work will be executed at the specified absolute time.
 */
template <concepts::dispatch_work Work, typename Clock, typename Duration>
RIWO_CORE_TAPI work_canceller_t post (
	const time_point<Clock,Duration> &atime, Work &&work
);

// Temporarily start an executor in the current context to perform a work.
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
RIWO_CORE_TAPI auto local_dispatch (
	concepts::exec_context auto &exec, Work &&work, Token &&token
);

// Temporarily start an executor in the current context to perform a work.
template <typename Work>
RIWO_CORE_TAPI auto local_dispatch(concepts::exec_context auto &exec, Work &&work)
	requires concepts::dispatch_token<detached_t, Work>;

// Temporarily start an executor in the current context to perform a work.
template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
RIWO_CORE_TAPI auto local_dispatch (
	Work &&work, Token &&token
);

// Temporarily start an executor in the current context to perform a work.
template <typename Work>
RIWO_CORE_TAPI auto local_dispatch(Work &&work)
	requires concepts::dispatch_token<detached_t, Work>;

namespace concepts
{

template <typename Token>
concept sleep_opt_token = []() consteval -> bool
{
	using token_t = std::remove_cvref_t<Token>;
	return not is_deferred_v<token_t> and
		opt_token<token_t,error_code>;
}();

template <typename Token>
concept co_sleep_opt_token =
	not is_sync_opt_token_v<Token> and
	sleep_opt_token<Token>;

} //namespace concepts

template <typename Rep, typename Period, concepts::co_sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORE_TAPI auto sleep_for (
	concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Token &&token = use_awaitable
);

template <typename Rep, typename Period, concepts::sleep_opt_token Token = use_sync_t>
[[nodiscard]] RIWO_CORE_TAPI auto sleep_for (
	const duration<Rep,Period> &rtime, Token &&token = {}
);

template <typename Clock, typename Duration, concepts::co_sleep_opt_token Token = const use_awaitable_t&>
[[nodiscard]] RIWO_CORE_TAPI auto sleep_until (
	concepts::sched auto &&exec, const time_point<Clock,Duration> &atime, Token &&token = use_awaitable
);

template <typename Clock, typename Duration, concepts::sleep_opt_token Token = use_sync_t>
[[nodiscard]] RIWO_CORE_TAPI auto sleep_until (
	const time_point<Clock,Duration> &atime, Token &&token = {}
);

namespace concepts
{

template <typename Work>
concept timer_work = dispatch_work<Work> or callable<Work,work_canceller_t>;

} //namespace concepts

template <concepts::timer_work Work, typename Rep, typename Period>
RIWO_CORE_TAPI work_canceller_t start_timer (
	concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Work &&work, bool immediately = false
);

template <concepts::timer_work Work, typename Rep, typename Period>
RIWO_CORE_TAPI work_canceller_t start_timer (
	const duration<Rep,Period> &rtime, Work &&work, bool immediately = false
);

namespace concepts
{

template <typename Func, typename Handler>
concept async_wake_up = std::is_rvalue_reference_v<Handler> and (
	callable<Func,Handler&&> or callable<Func,Handler&&,asio::any_io_executor>
);

} //namespace concepts

template <concepts::exec Exec, typename...Args>
class RIWO_CORE_TAPI basic_async_work
{
	RIWO_DISABLE_COPY_MOVE(basic_async_work)

public:
	using handler_t = asio::detail::awaitable_handler<Exec,Args...>;
	basic_async_work() = default;

	template <typename T>
	static constexpr bool is_wake_up_v =
		concepts::async_wake_up<T,handler_t&&>;

	template <typename T>
	static constexpr bool is_token_v =
		concepts::async_opt_token<T,Args...>;

public:
	template <typename WakeUp, typename Token = const use_awaitable_t&>
	[[nodiscard]] static auto handle
	(concepts::sched auto &&executor_arg, WakeUp &&wake_up_arg, Token &&token = use_awaitable)
		requires is_wake_up_v<WakeUp> and is_token_v<Token>;

	template <typename WakeUp, typename Token = const use_awaitable_t&>
	[[nodiscard]] static auto handle(WakeUp &&wake_up_arg, Token &&token = use_awaitable)
		requires is_wake_up_v<WakeUp> and is_token_v<Token>;
};

template <typename...Args>
using async_work = basic_async_work<asio::any_io_executor, Args...>;

// Delayed destructor.
RIWO_CORE_TAPI void delete_later(const concepts::exec auto &exec, auto *obj);
RIWO_CORE_TAPI void delete_later(concepts::exec_context auto &exec, auto *obj);
RIWO_CORE_TAPI void delete_later(auto *obj);

template <typename NativeExec>
struct is_match_def_exec
{
	static constexpr bool value =
		is_exec_v<NativeExec> and
		requires {
			NativeExec(get_executor());
		};
};

template <typename NativeExec>
constexpr bool is_match_def_exec_v = is_match_def_exec<NativeExec>::value;

namespace concepts
{

template <typename NativeExec>
concept match_def_exec = is_match_def_exec_v<NativeExec>;

}} //namespace riwo::concepts
#include <riwo/core/detail/execution.h>


#endif //RIWO_CORE_EXECUTION_H
