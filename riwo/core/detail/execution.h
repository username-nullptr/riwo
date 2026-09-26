// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_EXECUTION_H
#define RIWO_CORE_DETAIL_EXECUTION_H

namespace riwo { namespace concepts::detail
{

template <typename WakeUp, typename...Args>
concept async_wake_up = requires(WakeUp wake_up, Args&&...args) {
	wake_up(std::move(args)...);
};

} //namespace concepts::detail

namespace detail
{

enum class schedule_kind
{
	dispatch,
	post
};

template <typename T>
struct awaitable_value {
	using type = T;
};

template <typename T>
struct awaitable_value<awaitable<T>> {
	using type = T;
};

template <typename Work, bool = is_awaitable_v<std::remove_cvref_t<Work>>>
struct execution_work_traits;

template <typename Work>
struct execution_work_traits<Work,true>
{
	using invocation_t = std::remove_cvref_t<Work>;
	using result_t = awaitable_value<invocation_t>::type;
	static constexpr bool asynchronous = true;
};

template <typename Work>
struct execution_work_traits<Work,false>
{
	using invocation_t = std::invoke_result_t<Work>;
	using result_t = awaitable_value<invocation_t>::type;
	static constexpr bool asynchronous = is_awaitable_v<invocation_t>;
};

template <typename Work>
using execution_result_t = execution_work_traits<Work>::result_t;

template <schedule_kind Kind, typename Exec, typename Handler>
RIWO_CORE_TAPI void schedule(Exec &&exec, Handler &&handler)
{
	if constexpr( Kind == schedule_kind::dispatch )
		asio::dispatch(std::forward<Exec>(exec), std::forward<Handler>(handler));
	else
		asio::post(std::forward<Exec>(exec), std::forward<Handler>(handler));
}

template <schedule_kind Kind, typename Work>
RIWO_CORE_TAPI awaitable<execution_result_t<Work>> scheduled_work(Work work)
{
	using traits_t = execution_work_traits<Work>;
	using result_t = traits_t::result_t;

	// co_spawn itself has dispatch semantics. Add an explicit scheduling point
	// so that riwo::post remains an unconditional queueing operation.
	if constexpr( Kind == schedule_kind::post )
		co_await asio::post(use_awaitable);

	if constexpr( is_awaitable_v<Work> )
	{
		if constexpr( std::is_void_v<result_t> )
			co_await std::move(work);
		else
			co_return co_await std::move(work);
	}
	else if constexpr( traits_t::asynchronous )
	{
		if constexpr( std::is_void_v<result_t> )
			co_await std::invoke(work);
		else
			co_return co_await std::invoke(work);
	}
	else
	{
		if constexpr( std::is_void_v<result_t> )
			std::invoke(work);
		else
			co_return std::invoke(work);
	}
	if constexpr( std::is_void_v<result_t> )
		co_return ;
}

template <schedule_kind Kind, typename Exec, typename Work, typename Token>
RIWO_CORE_TAPI decltype(auto) spawn(Exec &&exec, Work &&work, Token &&token)
{
	using work_t = std::decay_t<Work>;
	return asio::co_spawn(std::forward<Exec>(exec),
		scheduled_work<Kind,work_t>(std::forward<Work>(work)),
		std::forward<Token>(token)
	);
}

template <typename T>
RIWO_CORE_TAPI void promise_set_value(std::promise<T> &promise, auto &&func)
{
	try {
		if constexpr( std::is_void_v<T> )
		{
			std::invoke(std::forward<decltype(func)>(func));
			promise.set_value();
		}
		else
			promise.set_value(std::invoke(std::forward<decltype(func)>(func)));
	}
	catch(...) {
		promise.set_exception(std::current_exception());
	}
}

template <schedule_kind Kind, typename Exec, typename Work, typename Token>
RIWO_CORE_TAPI decltype(auto) submit(Exec &&exec, Work &&work, Token &&token)
{
	using work_t = std::remove_cvref_t<Work>;
	using traits_t = execution_work_traits<work_t>;
	using return_t = traits_t::result_t;
	using token_t = std::remove_cvref_t<Token>;
	using ntoken_t = token_unbound_t<token_t>;

	if constexpr( traits_t::asynchronous )
	{
		if constexpr( is_sync_opt_token_v<Token> )
		{
			return submit<Kind>(std::forward<Exec>(exec),
				std::forward<Work>(work), use_future
			).get();
		}
		else
		{
			return spawn<Kind>(std::forward<Exec>(exec),
				std::forward<Work>(work), std::forward<Token>(token)
			);
		}
	}
	else
	{
		if constexpr( is_detached_v<ntoken_t> )
			schedule<Kind>(std::forward<Exec>(exec), std::forward<Work>(work));

		else if constexpr( is_use_future_v<ntoken_t> )
		{
			std::promise<return_t> promise;
			auto future = promise.get_future();

			schedule<Kind>(std::forward<Exec>(exec),
			[promise = std::move(promise), func = std::forward<Work>(work)]() mutable {
				promise_set_value(promise, func);
			});
			return future;
		}
		else if constexpr( is_async_opt_token_v<ntoken_t> )
		{
			return spawn<Kind>(std::forward<Exec>(exec),
				std::forward<Work>(work), std::forward<Token>(token)
			);
		}
		else
		{
			return submit<Kind>(std::forward<Exec>(exec),
				std::forward<Work>(work), use_future
			).get();
		}
	}
}

template <typename ToDuration, typename Rep, typename Period>
[[nodiscard]] RIWO_CORE_TAPI ToDuration nonnegative_duration(const duration<Rep,Period> &value)
{
	if constexpr( std::is_signed_v<decltype(value.count())> )
	{
		if( value.count() <= 0 )
			return ToDuration(0);
	}
	else if( value.count() == 0 )
		return ToDuration(0);

	return std::chrono::duration_cast<ToDuration>(value);
}

template <typename Timer>
[[nodiscard]] RIWO_CORE_TAPI work_canceller_t make_timer_canceller
(std::shared_ptr<Timer> timer, std::shared_ptr<std::atomic_bool> cancelled)
{
	auto exec = timer->get_executor();
	return [timer = std::move(timer), cancelled = std::move(cancelled), exec = std::move(exec)]() mutable
	{
		if( cancelled->exchange(true, std::memory_order_acq_rel) )
			return ;

		asio::dispatch(exec, [timer = std::move(timer)]() mutable {
			timer->cancel();
		});
	};
}

template <typename Timer, typename Exec, typename Expiry, typename Work>
[[nodiscard]] RIWO_CORE_TAPI work_canceller_t post_after(Exec &&exec, Expiry &&expiry, Work &&work)
{
	auto scheduled_exec = get_executor_helper(std::forward<Exec>(exec));
	auto timer = std::make_shared<Timer>(scheduled_exec,
		std::forward<Expiry>(expiry)
	);
	auto cancelled = std::make_shared<std::atomic_bool>(false);
	auto canceller = make_timer_canceller(timer, cancelled);

	timer->async_wait([timer, cancelled,
		scheduled_exec = std::move(scheduled_exec),
		scheduled_work = std::forward<Work>(work)
	](const error_code &error) mutable
	{
		RIWO_UNUSED(timer);
		if( not cancelled->load(std::memory_order_acquire) and
			error != errc::operation_aborted )
			dispatch(std::move(scheduled_exec), std::move(scheduled_work));
	});
	return canceller;
}

class RIWO_CORE_API local_dispatch_state
{
	RIWO_DISABLE_COPY_MOVE(local_dispatch_state)

public:
	local_dispatch_state() = default;
	void finish() noexcept;

	[[nodiscard]] bool finished() const noexcept;
	void wait() const noexcept;

private:
	std::atomic_bool m_finished {false};
};

template <typename Result>
struct local_dispatch_result
{
	using type = std::pair <
		std::remove_cvref_t<Result>,
		std::shared_ptr<size_t>
	>;
};

template <>
struct local_dispatch_result<void> {
	using type = std::shared_ptr<size_t>;
};

template <typename Work>
using local_dispatch_result_t =
	local_dispatch_result<execution_result_t<Work>>::type;

template <typename Work>
RIWO_CORE_TAPI auto make_local_work
(Work &&work, std::shared_ptr<local_dispatch_state> state, std::shared_ptr<size_t> counter)
{
	using work_t = std::decay_t<Work>;
	using traits_t = execution_work_traits<work_t>;

	using result_t = traits_t::result_t;
	using local_result_t = local_dispatch_result_t<work_t>;

	if constexpr( traits_t::asynchronous )
	{
		return [
			state = std::move(state), counter = std::move(counter),
			func = work_t(std::forward<Work>(work))
		]() mutable -> awaitable<local_result_t>
		{
			using stored_result_t = std::conditional_t <
				std::is_void_v<result_t>, std::monostate, result_t
			>;
			std::exception_ptr exception;
			optional<stored_result_t> result;
			try {
				if constexpr( std::is_void_v<result_t> )
					co_await scheduled_work<schedule_kind::dispatch,work_t>(std::move(func));
				else
				{
					result.emplace(co_await scheduled_work<schedule_kind::dispatch,work_t>(
						std::move(func)
					));
				}
			}
			catch(...) {
				exception = std::current_exception();
			}
			// Boost.Asio before awaitable_thread_is_launching was introduced may
			// otherwise post co_spawn's completion after the work marks itself as
			// finished. Ensure completion can be dispatched inline before the local
			// event pump observes the finished state.
			co_await asio::this_coro::throw_if_cancelled(false);
			co_await asio::post(use_awaitable);

			state->finish();
			if( exception )
				std::rethrow_exception(exception);

			if constexpr( std::is_void_v<result_t> )
				co_return counter;
			else
				co_return std::make_pair(std::move(*result), counter);
		};
	}
	else
	{
		return [
			state = std::move(state), counter = std::move(counter),
			func = work_t(std::forward<Work>(work))
		]() mutable -> local_result_t
		{
			try {
				if constexpr( std::is_void_v<result_t> )
				{
					std::invoke(func);
					state->finish();
					return counter;
				}
				else
				{
					auto result = std::invoke(func);
					state->finish();
					return std::make_pair(std::move(result), counter);
				}
			}
			catch(...)
			{
				state->finish();
				throw;
			}
		};
	}
}

template <typename Context>
RIWO_CORE_TAPI Context &local_context(std::reference_wrapper<Context> context) noexcept
{
	return context.get();
}

template <typename Context>
RIWO_CORE_TAPI Context &local_context(const std::shared_ptr<Context> &context) noexcept
{
	return *context;
}

template <typename Context>
concept pumpable_context = requires(Context &context)
{
	{ context.run_one_for(milliseconds(1)) } -> std::convertible_to<size_t>;
	{ context.stopped() } -> std::convertible_to<bool>;
	context.restart();
};

template <typename Context>
RIWO_CORE_TAPI size_t run_local_pump(Context &context, const std::shared_ptr<local_dispatch_state> &state)
{
	if constexpr( pumpable_context<Context> )
	{
		if( context.stopped() )
			context.restart();

		size_t counter = 0;
		do {
			counter += context.run_one_for(milliseconds(1));
		}
		while( not state->finished() );
		return counter;
	}
	else
	{
		// Some execution contexts (for example asio::thread_pool) own their
		// event pump. In that case local_dispatch only waits for completion.
		state->wait();
		return 0;
	}
}

template <typename ContextHolder, typename Work, typename Token>
RIWO_CORE_TAPI decltype(auto) local_dispatch_async(
	ContextHolder context, Work &&work, Token &&token)
{
	auto &exec = local_context(context);
	auto state = std::make_shared<local_dispatch_state>();
	auto counter = std::make_shared<size_t>(0);

	auto poll_work = asio::make_work_guard(exec);
	auto local_work = make_local_work(std::forward<Work>(work), state, counter);

	using async_result_t = decltype(dispatch(exec,
		std::move(local_work), std::forward<Token>(token))
	);
	if constexpr( std::is_void_v<async_result_t> )
	{
		dispatch(exec, std::move(local_work), std::forward<Token>(token));
		std::thread(
		[context = std::move(context), state, counter, poll_work = std::move(poll_work)]() mutable
		{
			RIWO_UNUSED(poll_work);
			*counter = run_local_pump(local_context(context), state);
		})
		.detach();
	}
	else
	{
		auto result = dispatch(exec, std::move(local_work), std::forward<Token>(token));
		std::thread(
		[context = std::move(context), state, counter, poll_work = std::move(poll_work)]() mutable
		{
			RIWO_UNUSED(poll_work);
			*counter = run_local_pump(local_context(context), state);
		})
		.detach();
		return result;
	}
}

template <typename ContextHolder, typename Work>
RIWO_CORE_TAPI auto local_dispatch_sync(ContextHolder context, Work &&work)
{
	using work_t = std::remove_cvref_t<Work>;
	using traits_t = execution_work_traits<work_t>;
	using result_t = traits_t::result_t;

	if constexpr( traits_t::asynchronous )
	{
		auto &exec = local_context(context);
		auto state = std::make_shared<local_dispatch_state>();
		auto counter = std::make_shared<size_t>(0);

		auto poll_work = asio::make_work_guard(exec);
		auto local_work = make_local_work(std::forward<Work>(work), state, counter);
		auto future = dispatch(exec, std::move(local_work), use_future);

		RIWO_UNUSED(poll_work);
		*counter = run_local_pump(exec, state);
		return future.get();
	}
	else if constexpr( std::is_void_v<result_t> )
	{
		std::invoke(std::forward<Work>(work));
		return std::make_shared<size_t>(1);
	}
	else
	{
		return std::make_pair(std::invoke(std::forward<Work>(work)),
			std::make_shared<size_t>(1)
		);
	}
}

template <typename ContextHolder, typename Work>
RIWO_CORE_TAPI jthread local_dispatch_thread(ContextHolder context, Work &&work)
{
	auto &exec = local_context(context);
	auto state = std::make_shared<local_dispatch_state>();
	auto counter = std::make_shared<size_t>(0);

	auto poll_work = asio::make_work_guard(exec);
	auto local_work = make_local_work(std::forward<Work>(work), state, counter);
	dispatch(exec, std::move(local_work), detached);

	return jthread (
	[context = std::move(context), state, counter, poll_work = std::move(poll_work)]() mutable
	{
		RIWO_UNUSED(poll_work);
		*counter = run_local_pump(local_context(context), state);
	});
}

template <typename Token>
[[nodiscard]] RIWO_CORE_TAPI error_code sleep_error(Token &token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_cancellation_slot_binder_v<token_t> )
		return sleep_error(token.get());

	else if constexpr( is_redirect_error_v<token_t> )
		return token.ec_;
	else
		return {};
}

template <typename Exec, typename Time, typename Token>
[[nodiscard]] awaitable<error_code> co_sleep_for(Exec exec, Time rtime, Token token)
{
	asio::steady_timer timer(std::move(exec),
		nonnegative_duration<asio::steady_timer::duration>(rtime)
	);
	co_await timer.async_wait(std::move(token));
	co_return sleep_error(token);
}

template <typename Time, typename Token>
[[nodiscard]] awaitable<error_code> co_sleep_for(Time rtime, Token token)
{
	co_return co_await co_sleep_for(co_await asio::this_coro::executor,
		std::move(rtime), std::move(token)
	);
}

template <typename Exec, typename Clock, typename Duration, typename Token>
[[nodiscard]] awaitable<error_code> co_sleep_until(Exec exec, time_point<Clock,Duration> atime, Token token)
{
	asio::basic_waitable_timer<Clock> timer(std::move(exec), atime);
	co_await timer.async_wait(std::move(token));
	co_return sleep_error(token);
}

template <typename Clock, typename Duration, typename Token>
[[nodiscard]] awaitable<error_code> co_sleep_until(time_point<Clock,Duration> atime, Token token)
{
	co_return co_await co_sleep_until(co_await asio::this_coro::executor,
		std::move(atime), std::move(token)
	);
}

template <typename Timer, typename Exec, typename Expiry, typename Token>
RIWO_CORE_TAPI decltype(auto) async_sleep(Exec &&exec, Expiry &&expiry, Token &&token)
{
	auto timer = std::make_shared<Timer>(
		get_executor_helper(std::forward<Exec>(exec)),
		std::forward<Expiry>(expiry)
	);
	return timer->async_wait(asio::consign(
		std::forward<Token>(token), std::move(timer)));
}

template <typename...Args>
struct initiate_token {
	using type = void(Args...);
};

template <>
struct initiate_token<void> {
	using type = void();
};

template <>
struct initiate_token<> : initiate_token<void> {};

template <typename...Args>
using initiate_token_t = initiate_token<Args...>::type;

template <typename Exec, typename WakeUp, typename Handler>
RIWO_CORE_TAPI void async_xx(const Exec &exec, WakeUp &&wake_up, Handler &&handler)
{
	using handler_t = std::remove_cvref_t<decltype(handler)>;
	using exec_t = std::remove_cvref_t<decltype(exec)>;

	if constexpr( concepts::detail::async_wake_up<WakeUp, handler_t, exec_t> )
		wake_up(std::forward<Handler>(handler), exec);
	else if constexpr( concepts::detail::async_wake_up<WakeUp, handler_t> )
		wake_up(std::forward<Handler>(handler));
	else
		static_assert(false, "Invalid function signature for async");
}

} //namespace detail

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
decltype(auto) dispatch(concepts::sched auto &&exec, Work &&work, Token &&token)
{
	return detail::submit<detail::schedule_kind::dispatch>(
		std::forward<decltype(exec)>(exec), std::forward<Work>(work), std::forward<Token>(token)
	);
}

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
decltype(auto) dispatch(Work &&work, Token &&token)
{
	return dispatch(io_context(), std::forward<Work>(work), std::forward<Token>(token));
}

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
decltype(auto) post(concepts::sched auto &&exec, Work &&work, Token &&token)
{
	return detail::submit<detail::schedule_kind::post>(
		std::forward<decltype(exec)>(exec), std::forward<Work>(work), std::forward<Token>(token)
	);
}

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
decltype(auto) post(Work &&work, Token &&token)
{
	return post(io_context(), std::forward<Work>(work), std::forward<Token>(token));
}

template <concepts::dispatch_work Work, typename Rep, typename Period>
work_canceller_t post(concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Work &&work)
{
	return detail::post_after<asio::steady_timer>(
		std::forward<decltype(exec)>(exec),
		detail::nonnegative_duration<asio::steady_timer::duration>(rtime),
		std::forward<Work>(work));
}

template <concepts::dispatch_work Work, typename Rep, typename Period>
work_canceller_t post(const duration<Rep,Period> &rtime, Work &&work)
{
	return post(io_context(), rtime, std::forward<Work>(work));
}

template <concepts::dispatch_work Work, typename Clock, typename Duration>
work_canceller_t post(concepts::sched auto &&exec, const time_point<Clock,Duration> &atime, Work &&work)
{
	return detail::post_after<asio::basic_waitable_timer<Clock>>(
		std::forward<decltype(exec)>(exec), atime,
		std::forward<Work>(work));
}

template <concepts::dispatch_work Work, typename Clock, typename Duration>
work_canceller_t post(const time_point<Clock,Duration> &atime, Work &&work)
{
	return post(io_context(), atime, std::forward<Work>(work));
}

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
auto local_dispatch(concepts::exec_context auto &exec, Work &&work, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;
	if constexpr( is_sync_opt_token_v<token_t> )
		return detail::local_dispatch_sync(std::ref(exec), std::forward<Work>(work));
	else
	{
		return detail::local_dispatch_async(std::ref(exec),
			std::forward<Work>(work), std::forward<Token>(token)
		);
	}
}

template <typename Work>
auto local_dispatch(concepts::exec_context auto &exec, Work &&work)
	requires concepts::dispatch_token<detached_t, Work>
{
	return detail::local_dispatch_thread(std::ref(exec), std::forward<Work>(work));
}

template <concepts::dispatch_work Work, concepts::dispatch_token<Work> Token>
auto local_dispatch(Work &&work, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;

	if constexpr( is_sync_opt_token_v<token_t> )
	{
		auto context = std::make_shared<asio::io_context>();
		return detail::local_dispatch_sync(std::move(context), std::forward<Work>(work));
	}
	else
	{
		// An asynchronous result may resume its continuation after the work
		// itself has completed. A temporary io_context cannot safely destroy
		// itself on its runner thread during that hand-off. Use Asio's
		// process-wide executor, whose lifetime is independent of the operation.
		auto state = std::make_shared<detail::local_dispatch_state>();
		auto counter = std::make_shared<size_t>(0);

		auto local_work = detail::make_local_work (
			std::forward<Work>(work), std::move(state), std::move(counter)
		);
		return dispatch(asio::system_executor{},
			std::move(local_work), std::forward<Token>(token)
		);
	}
}

template <typename Work>
auto local_dispatch(Work &&work)
	requires concepts::dispatch_token<detached_t, Work>
{
	return detail::local_dispatch_thread (
		std::make_shared<asio::io_context>(), std::forward<Work>(work)
	);
}

template <typename Rep, typename Period, concepts::co_sleep_opt_token Token>
auto sleep_for(concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;
	if constexpr( is_use_awaitable_v<token_t> )
	{
		return detail::co_sleep_for(
			get_executor_helper(std::forward<decltype(exec)>(exec)),
			rtime, std::forward<Token>(token)
		);
	}
	else
	{
		return detail::async_sleep<asio::steady_timer>(std::forward<decltype(exec)>(exec),
			detail::nonnegative_duration<asio::steady_timer::duration>(rtime),
			std::forward<Token>(token)
		);
	}
}

template <typename Rep, typename Period, concepts::sleep_opt_token Token>
auto sleep_for(const duration<Rep,Period> &rtime, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;
	if constexpr( is_sync_opt_token_v<token_t> )
		std::this_thread::sleep_for(rtime);

	else if constexpr( is_use_awaitable_v<token_t> )
		return detail::co_sleep_for(rtime, std::forward<Token>(token));
	else
		return sleep_for(get_executor(), rtime, std::forward<Token>(token));
}

template <typename Clock, typename Duration, concepts::co_sleep_opt_token Token>
auto sleep_until(concepts::sched auto &&exec, const time_point<Clock,Duration> &atime, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;
	if constexpr( is_use_awaitable_v<token_t> )
	{
		return detail::co_sleep_until (
			get_executor_helper(std::forward<decltype(exec)>(exec)),
			atime, std::forward<Token>(token)
		);
	}
	else
	{
		return detail::async_sleep<asio::basic_waitable_timer<Clock>>(
			std::forward<decltype(exec)>(exec), atime, std::forward<Token>(token));
	}
}

template <typename Clock, typename Duration, concepts::sleep_opt_token Token>
auto sleep_until(const time_point<Clock,Duration> &atime, Token &&token)
{
	using token_t = token_unbound_t<std::remove_cvref_t<Token>>;
	if constexpr( is_sync_opt_token_v<token_t> )
		std::this_thread::sleep_until(atime);

	else if constexpr( is_use_awaitable_v<token_t> )
		return detail::co_sleep_until(atime, std::forward<Token>(token));
	else
		return sleep_until(get_executor(), atime, std::forward<Token>(token));
}

template <concepts::timer_work Work, typename Rep, typename Period>
work_canceller_t start_timer
(concepts::sched auto &&exec, const duration<Rep,Period> &rtime, Work &&work, bool immediately)
{
	if( rtime.count() <= 0 )
	{
		runtime_error::loc_throw (
			"riwo::start_timer: Invalid time duration"
		);
	}
	auto scheduled_exec = get_executor_helper(std::forward<decltype(exec)>(exec));
	auto timer = std::make_shared<asio::steady_timer>(scheduled_exec);

	auto cancelled = std::make_shared<std::atomic_bool>(false);
	auto canceller = detail::make_timer_canceller(timer, cancelled);

	riwo::dispatch(std::move(scheduled_exec), [
		timer_ptr = std::move(timer), cancel_flag = std::move(cancelled), canceller,
		delay = std::chrono::duration_cast<asio::steady_timer::duration>(rtime),
		func = std::forward<Work>(work), immediately
	]() mutable -> awaitable<void>
	{
		using namespace operators;
		error_code error;

		auto atime = std::chrono::steady_clock::now();
		auto sleep = [&]() -> awaitable<bool>
		{
			if( cancel_flag->load(std::memory_order_acquire) )
				co_return false;

			atime += delay;
			timer_ptr->expires_at(atime);

			co_await timer_ptr->async_wait(use_awaitable | error);
			if( cancel_flag->load(std::memory_order_acquire) )
				co_return false;

			co_return not error;
		};
		if( not immediately )
		{
			if( not co_await sleep() )
				co_return ;
		}
		for(;;)
		{
			if constexpr( concepts::callable<Work,work_canceller_t> )
			{
				using return_t = decltype(std::declval<Work>()(canceller));
				if constexpr( is_awaitable_v<return_t> )
					co_await std::invoke(func, canceller);
				else
					std::invoke(func, canceller);
			}
			else
			{
				using return_t = std::invoke_result_t<Work>;
				if constexpr( is_awaitable_v<return_t> )
					co_await std::invoke(func);
				else
					std::invoke(func);
			}
			if( not co_await sleep() )
				break;
		}
		co_return ;
	});
	return canceller;
}

template <concepts::timer_work Work, typename Rep, typename Period>
work_canceller_t start_timer(const duration<Rep,Period> &rtime, Work &&work, bool immediately)
{
	return start_timer(io_context(), rtime, std::forward<Work>(work), immediately);
}

template <concepts::exec Exec, typename...Args>
template <typename WakeUp, typename Token>
auto basic_async_work<Exec,Args...>::handle
(concepts::sched auto &&executor_arg, WakeUp &&wake_up_arg, Token &&token)
	requires is_wake_up_v<WakeUp> and is_token_v<Token>
{
	using token_t = std::remove_cvref_t<Token>;
	using func_t = decltype(wake_up_arg);
	auto ntoken = unbound_redirect_time(std::forward<Token>(token));

	return asio::async_initiate<token_t, detail::initiate_token_t<Args...>> (
	[exec = get_executor_helper(executor_arg), wake_up = std::forward<func_t>(wake_up_arg)](auto handler) mutable
	{
		auto work = asio::make_work_guard(handler);
		asio::post(exec, [
			inner_exec = work.get_executor(), inner_work = std::move(work),
			inner_wake_up = std::move(wake_up), inner_handler = std::move(handler)
		]() mutable
		{
			RIWO_UNUSED(inner_work);
			detail::async_xx(inner_exec, std::move(inner_wake_up), std::move(inner_handler));
		});
	},
	ntoken);
}

template <concepts::exec Exec, typename...Args>
template <typename WakeUp, typename Token>
auto basic_async_work<Exec,Args...>::handle(WakeUp &&wake_up_arg, Token &&token)
	requires is_wake_up_v<WakeUp> and is_token_v<Token>
{
	using token_t = std::remove_cvref_t<Token>;
	using func_t = decltype(wake_up_arg);
	auto ntoken = unbound_redirect_time(std::forward<Token>(token));

	return asio::async_initiate<token_t, detail::initiate_token_t<Args...>> (
	[wake_up = std::forward<func_t>(wake_up_arg)](auto handler) mutable
	{
		auto work = asio::make_work_guard(handler);
		auto exec = work.get_executor();

		asio::post(exec, [
			exec, inner_work = std::move(work), inner_wake_up = std::move(wake_up),
			inner_handler = std::move(handler)
		]() mutable
		{
			RIWO_UNUSED(inner_work);
			detail::async_xx(exec, std::move(inner_wake_up), std::move(inner_handler));
		});
	},
	ntoken);
}

void delete_later(const concepts::exec auto &exec, auto *obj)
{
	asio::post(exec, [obj]{ delete obj; });
}

void delete_later(concepts::exec_context auto &exec, auto *obj)
{
	asio::post(exec, [obj]{ delete obj; });
}

void delete_later(auto *obj)
{
	delete_later(io_context(), obj);
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_EXECUTION_H
