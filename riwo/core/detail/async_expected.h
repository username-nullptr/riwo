// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_ASYNC_EXPECTED_H
#define RIWO_CORE_DETAIL_ASYNC_EXPECTED_H

namespace riwo { namespace detail
{

[[nodiscard]] RIWO_CORE_API error_code canonical_error(error_code error) noexcept;

template <typename Value>
RIWO_CORE_TAPI void canonicalize_expected(sys_expected<Value> &expected) noexcept
{
	if( not expected )
		expected = sys_unexpected(canonical_error(expected.error()));
}

template <typename>
struct is_async_argument_reference : std::false_type {};

template <typename T>
struct is_async_argument_reference<std::reference_wrapper<T>> : std::true_type {};

template <typename Value, typename Factory>
[[nodiscard]] RIWO_CORE_TAPI
awaitable<sys_expected<Value>> co_expected_direct(Factory factory) {
	co_return co_await factory();
}

template <typename Value, typename Exec>
class RIWO_CORE_TAPI timed_expected_state : public
	std::enable_shared_from_this<timed_expected_state<Value,Exec>>
{
	using self_t = timed_expected_state;
	using result_t = sys_expected<Value>;
	using completion_fn_t = void (*)(self_t&, std::exception_ptr, result_t, bool);

	enum class first_success { none, operation, timer };

	struct operation_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<self_t> state {};

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
			return state->m_operation_cancellation.slot();
		}
		void operator()(std::exception_ptr exception, result_t result) {
			state->operation_complete(std::move(exception), std::move(result));
		}
	};

	struct timer_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<self_t> state {};

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
			return state->m_timer_cancellation.slot();
		}
		void operator()(error_code error) {
			state->timer_complete(error);
		}
	};

	struct caller_cancellation_handler
	{
		std::weak_ptr<self_t> state {};

		void operator()(asio::cancellation_type_t type)
		{
			if( auto active = state.lock() )
				active->cancel(type);
		}
	};

public:
	timed_expected_state(const Exec &exec, asio::cancellation_slot caller_slot,
		std::chrono::nanoseconds timeout, completion_fn_t completion_fn) :
		m_exec(exec), m_timer(exec), m_caller_slot(caller_slot),
		m_completion_fn(completion_fn)
	{
		m_timer.expires_after(timeout);
	}

	template <typename Factory>
	void start(Factory factory)
	{
		auto self = this->shared_from_this();
		try {
			if( m_caller_slot.is_connected() )
			{
				m_caller_slot.emplace<caller_cancellation_handler>(
					std::weak_ptr<self_t>(self)
				);
			}
			auto operation = factory();
			asio::co_spawn(m_exec, std::move(operation), operation_handler{self});

			m_timer.async_wait(timer_handler{std::move(self)});
			finish_launch();
		}
		catch(...) {
			fail(std::current_exception());
		}
	}

protected:
	[[nodiscard]] const Exec &executor() const noexcept {
		return m_exec;
	}

private:
	void finish_launch()
	{
		auto operation_type = asio::cancellation_type::none;
		auto timer_type = asio::cancellation_type::none;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			m_launching = false;
			operation_type = std::exchange(m_pending_operation_cancellation,
				asio::cancellation_type::none
			);
			timer_type = std::exchange(m_pending_timer_cancellation,
				asio::cancellation_type::none
			);
		}
		emit_cancellation(operation_type, timer_type);
	}

	void cancel(asio::cancellation_type_t type)
	{
		if( type == asio::cancellation_type::none )
			return ;

		bool emit = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			if( m_launching )
			{
				m_pending_operation_cancellation |= type;
				m_pending_timer_cancellation |= type;
			}
			else
				emit = true;
		}
		if( emit )
			emit_cancellation(type, type);
	}

	void operation_complete(std::exception_ptr exception, result_t result)
	{
		bool cancel_timer = false;
		bool complete = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or m_operation_done )
				return ;

			m_operation_exception = std::move(exception);
			if( not m_operation_exception )
			{
				if( m_first_success == first_success::none )
					m_first_success = first_success::operation;

				m_result.emplace(std::move(result));
				if( not m_timer_done )
				{
					if( m_launching )
						m_pending_timer_cancellation |= asio::cancellation_type::all;
					else
						cancel_timer = true;
				}
			}
			m_operation_done = true;
			complete = m_timer_done;
		}
		if( cancel_timer )
		{
			emit_cancellation(asio::cancellation_type::none,
				asio::cancellation_type::all
			);
		}
		if( complete )
			complete_normal();
	}

	void timer_complete(error_code error)
	{
		bool cancel_operation = false;
		bool complete = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or m_timer_done )
				return ;

			m_timer_error = canonical_error(error);
			if( not m_timer_error )
			{
				if( m_first_success == first_success::none )
					m_first_success = first_success::timer;

				if( not m_operation_done )
				{
					if( m_launching )
						m_pending_operation_cancellation |= asio::cancellation_type::all;
					else
						cancel_operation = true;
				}
			}
			m_timer_done = true;
			complete = m_operation_done;
		}
		if( cancel_operation )
		{
			emit_cancellation(asio::cancellation_type::all,
				asio::cancellation_type::none
			);
		}
		if( complete )
			complete_normal();
	}

	void complete_normal()
	{
		std::exception_ptr exception;
		optional<result_t> result;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or not m_operation_done or not m_timer_done )
				return ;

			m_completed = true;
			if( m_first_success == first_success::operation )
				result.emplace(std::move(*m_result));

			else if( m_first_success == first_success::timer )
			{
				result.emplace(sys_unexpected (
					make_error_code(errc::timed_out))
				);
			}
			else
			{
				const auto error = make_system_error_code(std::errc::io_error);
				exception = std::make_exception_ptr(std::system_error(error));
				result.emplace(sys_unexpected(error));
			}
		}
		m_caller_slot.clear();
		m_completion_fn(*this, std::move(exception), std::move(*result), false);
	}

	void fail(std::exception_ptr exception)
	{
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			m_completed = true;
			m_launching = false;
		}
		m_caller_slot.clear();
		emit_cancellation(asio::cancellation_type::all,
			asio::cancellation_type::all
		);
		m_completion_fn(*this, std::move(exception),
			result_t(sys_unexpected(make_system_error_code(std::errc::io_error))), true
		);
	}

	void emit_cancellation
	(asio::cancellation_type_t operation_type, asio::cancellation_type_t timer_type)
	{
		std::lock_guard emit_lock(m_emit_mutex);
		if( operation_type != asio::cancellation_type::none )
			m_operation_cancellation.emit(operation_type);

		if( timer_type != asio::cancellation_type::none )
			m_timer_cancellation.emit(timer_type);
	}

	Exec m_exec {};
	asio::steady_timer m_timer;

	asio::cancellation_signal m_operation_cancellation;
	asio::cancellation_signal m_timer_cancellation;
	asio::cancellation_slot m_caller_slot;
	completion_fn_t m_completion_fn;

	std::mutex m_mutex;
	std::mutex m_emit_mutex;

	std::optional<result_t> m_result;
	std::exception_ptr m_operation_exception;

	error_code m_timer_error;
	first_success m_first_success = first_success::none;

	asio::cancellation_type_t m_pending_operation_cancellation =
		asio::cancellation_type::none;

	asio::cancellation_type_t m_pending_timer_cancellation =
		asio::cancellation_type::none;

	bool m_launching = true;
	bool m_operation_done = false;
	bool m_timer_done = false;
	bool m_completed = false;
};

template <typename Value, typename Exec, typename Handler>
class RIWO_CORE_TAPI timed_expected_operation final :
	public timed_expected_state<Value,Exec>
{
	using self_t = timed_expected_operation;
	using base_t = timed_expected_state<Value,Exec>;
	using result_t = sys_expected<Value>;

public:
	timed_expected_operation(const Exec &exec, Handler handler, std::chrono::nanoseconds timeout) :
		base_t(exec, asio::get_associated_cancellation_slot(handler), timeout, &self_t::complete),
		m_handler(std::move(handler)) {}

private:
	static void complete(base_t &base, std::exception_ptr exception,
		result_t result, bool post)
	{
		auto &self = static_cast<self_t&>(base);
		auto handler = std::move(*self.m_handler);

		self.m_handler.reset();
		if( post )
		{
			riwo::post_completion(self.executor(), std::move(handler),
				std::move(exception), std::move(result));
			return ;
		}
		auto completion_exec = asio::get_associated_executor(handler, self.executor());
		auto allocator = asio::get_associated_allocator(handler);

		auto completion = asio::bind_allocator(allocator, [
			handler = std::move(handler), exception = std::move(exception),
			result = std::move(result)
		]() mutable {
			std::move(handler)(std::move(exception), std::move(result));
		});
		asio::dispatch(completion_exec, std::move(completion));
	}

	optional<Handler> m_handler;
};

template <typename Value, typename Exec, typename Factory, typename Handler>
void start_timed_expected
(const Exec &exec, Factory factory, std::chrono::nanoseconds timeout, Handler &&handler)
{
	auto allocator = asio::get_associated_allocator(handler);
	using handler_t = std::remove_cvref_t<Handler>;
	using state_t = timed_expected_operation<Value,Exec,handler_t>;

	auto state = std::allocate_shared<state_t>(allocator, exec,
		std::forward<Handler>(handler), timeout
	);
	state->start(std::move(factory));
}

template <bool Timed, typename Value>
class expected_launcher;

template <typename Value>
class RIWO_CORE_TAPI expected_launcher<false,Value>
{
public:
	explicit expected_launcher(std::chrono::nanoseconds) noexcept {}

	template <typename Exec, typename Factory, typename Handler>
	void operator()(const Exec &exec, Factory operation, Handler handler) const
	{
		asio::co_spawn(exec,
			co_expected_direct<Value>(std::move(operation)),
			std::move(handler)
		);
	}
};

template <typename Value>
class RIWO_CORE_TAPI expected_launcher<true,Value>
{
public:
	explicit expected_launcher(std::chrono::nanoseconds timeout) noexcept :
		m_timeout(timeout) {}

	template <typename Exec, typename Factory, typename Handler>
	void operator()(const Exec &exec, Factory operation, Handler handler) const
	{
		start_timed_expected<Value>(exec,
			std::move(operation), m_timeout, std::move(handler)
		);
	}

private:
	std::chrono::nanoseconds m_timeout {};
};

template <bool Timed, typename Value, typename Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_expected
(const Exec &exec, Factory factory_fn, std::chrono::nanoseconds timeout, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code,Value)>(
	[exec, operation = std::move(factory_fn), launcher = expected_launcher<Timed,Value>(timeout)]
	(auto completion_handler) mutable
	{
		auto slot = asio::get_associated_cancellation_slot(completion_handler);
		auto completion_exec = asio::get_associated_executor (
			completion_handler, exec
		);
		auto allocator = asio::get_associated_allocator(completion_handler);

		auto handler = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
			asio::bind_cancellation_slot(slot, [handler = std::move(completion_handler)]
			(const std::exception_ptr &exception, sys_expected<Value> result) mutable
			{
				if( auto error = exception_error(exception) )
				{
					std::move(handler)(error, Value{});
					return ;
				}
				canonicalize_expected(result);

				if( not result )
					std::move(handler)(result.error(), Value{});
				else
					std::move(handler)(error_code{}, std::move(*result));
			})
		));
		launcher(exec, std::move(operation), std::move(handler));
	},
	completion_token);
}

template <typename>
struct token_has_redirect_error : std::false_type {};

template <typename Token>
struct token_has_redirect_error<redirect_error_t<Token>> : std::true_type {};

template <typename Token>
struct token_has_redirect_error<redirect_time_t<Token>> :
	token_has_redirect_error<Token> {};

template <typename Token, typename CancellationSlot>
struct token_has_redirect_error<cancellation_slot_binder<Token,CancellationSlot>> :
	token_has_redirect_error<Token> {};

template <typename Token>
constexpr bool token_has_redirect_error_v =
	token_has_redirect_error<std::remove_cvref_t<Token>>::value;

// Some APIs deliberately keep expected in their future/coroutine result.  This
// bridge still presents an error_code to redirect_error, but never turns that
// error into an exception for an unredirected future or awaitable.
template <bool Timed, typename Value, typename Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_preserved_expected
(const Exec &exec, Factory factory_fn, std::chrono::nanoseconds timeout, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	if constexpr( token_has_redirect_error_v<token_t> )
	{
		return asio::async_initiate<token_t,void(error_code,sys_expected<Value>)>(
		[exec, operation = std::move(factory_fn), launcher = expected_launcher<Timed,Value>(timeout)]
		(auto completion_handler) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_handler);
			auto completion_exec = asio::get_associated_executor (
				completion_handler, exec
			);
			auto allocator = asio::get_associated_allocator(completion_handler);

			auto handler = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
				asio::bind_cancellation_slot(slot, [handler = std::move(completion_handler)]
				(const std::exception_ptr &exception, sys_expected<Value> result) mutable
				{
					if( auto error = exception_error(exception) )
					{
						std::move(handler)(error,
							sys_expected<Value>(sys_unexpected(error))
						);
						return ;
					}
					canonicalize_expected(result);

					const auto error = result ? error_code{} : result.error();
					std::move(handler)(error, std::move(result));
				})
			));
			launcher(exec, std::move(operation), std::move(handler));
		},
		completion_token);
	}
	else
	{
		return asio::async_initiate<token_t,void(sys_expected<Value>)>(
		[exec, operation = std::move(factory_fn), launcher = expected_launcher<Timed,Value>(timeout)]
		(auto completion_handler) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_handler);
			auto completion_exec = asio::get_associated_executor (
				completion_handler, exec
			);
			auto allocator = asio::get_associated_allocator(completion_handler);

			auto handler = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
				asio::bind_cancellation_slot(slot, [handler = std::move(completion_handler)]
				(const std::exception_ptr &exception, sys_expected<Value> result) mutable
				{
					if( auto error = exception_error(exception) )
					{
						std::move(handler)(sys_expected<Value>(
							sys_unexpected(error)
						));
						return ;
					}
					canonicalize_expected(result);
					std::move(handler)(std::move(result));
				})
			));
			launcher(exec, std::move(operation), std::move(handler));
		},
		completion_token);
	}
}

template <typename Factory>
[[nodiscard]] RIWO_CORE_TAPI
awaitable<sys_expected<std::monostate>> co_void_expected_value(Factory factory)
{
	auto result = co_await factory();
	if( not result )
		co_return sys_unexpected(result.error());
	co_return std::monostate {};
}

template <bool Timed, typename Exec, typename Factory, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_expected_void
(const Exec &exec, Factory factory_fn, std::chrono::nanoseconds timeout, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code)>(
	[exec, operation = std::move(factory_fn), launcher = expected_launcher<Timed,std::monostate>(timeout)]
	(auto completion_handler) mutable
	{
		auto slot = asio::get_associated_cancellation_slot(completion_handler);
		auto completion_exec = asio::get_associated_executor (
			completion_handler, exec
		);
		auto allocator = asio::get_associated_allocator(completion_handler);
		auto value_operation = [void_operation = std::move(operation)]() mutable {
			return co_void_expected_value(std::move(void_operation));
		};
		auto handler = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
			asio::bind_cancellation_slot(slot, [handler = std::move(completion_handler)]
			(const std::exception_ptr &exception, sys_expected<std::monostate> result) mutable
			{
				if( auto error = exception_error(exception) )
					std::move(handler)(error);
				else if( not result )
					std::move(handler)(canonical_error(result.error()));
				else
					std::move(handler)(error_code{});
			})
		));
		launcher(exec, std::move(value_operation), std::move(handler));
	},
	completion_token);
}

// Ordinary completion tokens stay on the direct async_initiate path. Only a
// positive redirect_time creates the coroutine/timer race above.
template <typename Value, typename Handler, typename Exec>
class RIWO_CORE_TAPI direct_io_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using immediate_executor_type = asio::associated_immediate_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	direct_io_handler(Handler handler, const Exec &exec) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_immediate_executor(asio::get_associated_immediate_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] immediate_executor_type get_immediate_executor() const noexcept {
		return m_immediate_executor;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()(error_code error, Value value)
	{
		error = canonical_error(error);
		std::move(m_handler)(error, std::move(value));
	}

private:
	Handler m_handler;
	executor_type m_executor;
	immediate_executor_type m_immediate_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
};

template <typename Handler, typename Exec>
class RIWO_CORE_TAPI direct_io_void_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using immediate_executor_type = asio::associated_immediate_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	direct_io_void_handler(Handler handler, const Exec &exec) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_immediate_executor(asio::get_associated_immediate_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] immediate_executor_type get_immediate_executor() const noexcept {
		return m_immediate_executor;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()(error_code error)
	{
		error = canonical_error(error);
		std::move(m_handler)(error);
	}

private:
	Handler m_handler;
	executor_type m_executor;
	immediate_executor_type m_immediate_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
};

template <typename Value, typename Handler, typename Exec>
class RIWO_CORE_TAPI co_spawn_io_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	co_spawn_io_handler(Handler handler, const Exec &exec) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()(const std::exception_ptr &exception, std::tuple<error_code,Value> result)
	{
		if( auto error = exception_error(exception) )
			std::move(m_handler)(error, Value{});
		else
		{
			std::move(m_handler)(std::get<0>(result),
				std::move(std::get<1>(result))
			);
		}
	}

private:
	Handler m_handler;
	executor_type m_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
};

template <typename Handler, typename Exec>
class RIWO_CORE_TAPI co_spawn_error_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	co_spawn_error_handler(Handler handler, const Exec &exec) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()(const std::exception_ptr &exception, error_code error)
	{
		if( auto exception_code = exception_error(exception) )
			std::move(m_handler)(exception_code);
		else
			std::move(m_handler)(error);
	}

private:
	Handler m_handler;
	executor_type m_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
};

template <typename Value, typename Handler, typename Exec, typename Factory>
class RIWO_CORE_TAPI co_spawn_optional_io_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	co_spawn_optional_io_handler(Handler handler, const Exec &exec, Factory factory) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)),
		m_factory(std::move(factory)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()
	(const std::exception_ptr &exception, std::pair<error_code,std::optional<Value>> result)
	{
		if( auto error = exception_error(exception) )
			std::move(m_handler)(error, m_factory());

		else if( not result.second )
		{
			std::move(m_handler)(result.first ? result.first :
				make_system_error_code(std::errc::io_error), m_factory()
			);
		}
		else
			std::move(m_handler)(result.first, std::move(*result.second));
	}

private:
	Handler m_handler;
	executor_type m_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
	Factory m_factory;
};

template <typename Value, typename Handler, typename Exec, typename Factory>
class RIWO_CORE_TAPI awaitable_optional_tuple_io_handler
{
public:
	using executor_type = asio::associated_executor_t<Handler,Exec>;
	using allocator_type = asio::associated_allocator_t<Handler>;
	using cancellation_slot_type = asio::associated_cancellation_slot_t<Handler>;

	awaitable_optional_tuple_io_handler(Handler handler, const Exec &exec,
		Factory factory) :
		m_handler(std::move(handler)),
		m_executor(asio::get_associated_executor(m_handler, exec)),
		m_allocator(asio::get_associated_allocator(m_handler)),
		m_slot(asio::get_associated_cancellation_slot(m_handler)),
		m_factory(std::move(factory)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_executor;
	}
	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return m_allocator;
	}
	[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
		return m_slot;
	}

	void operator()
	(const std::exception_ptr &exception, std::optional<std::tuple<error_code,Value>> result)
	{
		if( auto error = exception_error(exception) )
			std::move(m_handler)(error, m_factory());

		else if( not result )
		{
			std::move(m_handler) (
				make_system_error_code(std::errc::io_error), m_factory()
			);
		}
		else
		{
			std::move(m_handler)(std::get<0>(*result),
				std::move(std::get<1>(*result))
			);
		}
	}

private:
	Handler m_handler;
	executor_type m_executor;
	allocator_type m_allocator;
	cancellation_slot_type m_slot;
	Factory m_factory;
};

// co_spawn provides a general-purpose executor/work-guard bridge.  The I/O
// adapters below already own their operation executor and forward completion
// through a handler that performs the final post, so that general bridge is
// unnecessarily expensive on every HTTP message.  This launcher keeps the
// awaitable body independent of the completion token while retaining the
// associated cancellation path and the asynchronous-completion guarantee.
template <typename Exec>
class RIWO_CORE_TAPI awaitable_cancellation_relay
{
public:
	explicit awaitable_cancellation_relay(const Exec &exec) :
		m_signal(std::make_shared<asio::cancellation_signal>()),
		m_exec(exec) {}

	[[nodiscard]] asio::cancellation_slot slot() const noexcept {
		return m_signal->slot();
	}

	void operator()(asio::cancellation_type_t type)
	{
		auto signal = m_signal;
		asio::dispatch(m_exec, [signal = std::move(signal), type]{
			signal->emit(type);
		});
	}

private:
	std::shared_ptr<asio::cancellation_signal> m_signal;
	Exec m_exec;
};

template <typename Value, typename Handler, typename Exec>
[[nodiscard]] RIWO_CORE_TAPI asio::awaitable<asio::detail::awaitable_thread_entry_point,Exec>
co_launch_awaitable(asio::awaitable<Value,Exec> operation, Handler handler)
{
#if RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
	co_await asio::detail::awaitable_thread_has_context_switched {} = false;
#endif //RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT

	std::exception_ptr exception;
	bool completed = false;
	try {
		Value result = co_await std::move(operation);
		completed = true;

#if RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
		if( not co_await asio::detail::awaitable_thread_has_context_switched {} )
#else //RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
		if( co_await asio::detail::awaitable_thread_is_launching {} )
#endif //RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
		{
			co_await asio::this_coro::throw_if_cancelled(false);
			co_await asio::post(deferred);
		}
		std::move(handler)(std::exception_ptr{}, std::move(result));
		co_return;
	}
	catch(...)
	{
		if( completed )
			throw;
		exception = std::current_exception();
	}
#if RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
	if( not (co_await asio::detail::awaitable_thread_has_context_switched{}) )
#else //RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
	if( co_await asio::detail::awaitable_thread_is_launching{} )
#endif //RIWO_ASIO_LEGACY_AWAITABLE_CONTEXT
	{
		co_await asio::this_coro::throw_if_cancelled(false);
		co_await asio::post(deferred);
	}
	std::move(handler)(std::move(exception), Value{});
}

template <typename Value, typename Handler, typename Exec>
RIWO_CORE_TAPI void launch_awaitable
(const Exec &exec, asio::awaitable<Value,Exec> operation, Handler handler)
{
	auto parent_slot = asio::get_associated_cancellation_slot(handler);
	using relay_t = awaitable_cancellation_relay<Exec>;

	auto *relay = parent_slot.is_connected() ?
		&parent_slot.template emplace<relay_t>(exec) : nullptr;

	auto child_slot = relay ? relay->slot() : asio::cancellation_slot{};
	auto entry = co_launch_awaitable(std::move(operation), std::move(handler));

	asio::detail::awaitable_handler<Exec,void>(std::move(entry), exec,
		child_slot, asio::cancellation_state(child_slot)
	).launch();
}

template <typename Value, typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI
auto initiate_io_direct(const Exec &exec, Initiator initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code,Value)>(
	[exec, start = std::move(initiation)]<typename Handler>(Handler completion_handler) mutable
	{
		start(direct_io_handler<Value,Handler,Exec>(
			std::move(completion_handler), exec)
		);
	},
	completion_token);
}

template <typename Value, typename Exec>
class RIWO_CORE_TAPI timed_io_state :
	public std::enable_shared_from_this<timed_io_state<Value,Exec>>
{
	using self_t = timed_io_state;
	using completion_fn_t = void (*)(self_t&, error_code, Value, bool);

	enum class first_completion {
		none, io, timer
	};

	struct io_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<self_t> state {};

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
			return state->m_io_cancellation.slot();
		}
		void operator()(error_code error, Value value) {
			state->io_complete(error, std::move(value));
		}
	};

	struct void_io_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<self_t> state {};

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
			return state->m_io_cancellation.slot();
		}
		void operator()(error_code error) {
			state->io_complete(error, Value{});
		}
	};

	struct timer_handler
	{
		using cancellation_slot_type = asio::cancellation_slot;
		std::shared_ptr<self_t> state {};

		[[nodiscard]] cancellation_slot_type get_cancellation_slot() const noexcept {
			return state->m_timer_cancellation.slot();
		}
		void operator()(error_code error) {
			state->timer_complete(error);
		}
	};

	struct caller_cancellation_handler
	{
		std::weak_ptr<self_t> state;

		void operator()(asio::cancellation_type_t type)
		{
			if( auto active = state.lock() )
				active->cancel(type);
		}
	};

public:
	timed_io_state(const Exec &exec, asio::cancellation_slot caller_slot,
		std::chrono::nanoseconds timeout, completion_fn_t completion_fn) :
		m_exec(exec), m_timer(exec), m_caller_slot(caller_slot),
		m_completion_fn(completion_fn)
	{
		m_timer.expires_after(timeout);
	}

	template <typename Initiator>
	void start(Initiator initiation)
	{
		auto self = this->shared_from_this();
		try {
			if( m_caller_slot.is_connected() )
			{
				m_caller_slot.emplace<caller_cancellation_handler>(
					std::weak_ptr<self_t>(self)
				);
			}
			if constexpr( std::same_as<Value,std::monostate> )
				initiation(void_io_handler{self});
			else
				initiation(io_handler{self});

			m_timer.async_wait(timer_handler{std::move(self)});
			finish_launch();
		}
		catch(...) {
			fail(std::current_exception());
		}
	}

protected:
	[[nodiscard]] const Exec& executor() const noexcept {
		return m_exec;
	}

private:
	void finish_launch()
	{
		auto io_type = asio::cancellation_type::none;
		auto timer_type = asio::cancellation_type::none;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			m_launching = false;
			io_type = std::exchange(m_pending_io_cancellation,
				asio::cancellation_type::none
			);
			timer_type = std::exchange(m_pending_timer_cancellation,
				asio::cancellation_type::none
			);
		}
		emit_cancellation(io_type, timer_type);
	}

	void cancel(asio::cancellation_type_t type)
	{
		if( type == asio::cancellation_type::none )
			return ;

		bool emit = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			if( m_launching )
			{
				m_pending_io_cancellation |= type;
				m_pending_timer_cancellation |= type;
			}
			else
				emit = true;
		}
		if( emit )
			emit_cancellation(type, type);
	}

	void io_complete(error_code error, Value value)
	{
		bool cancel_timer = false;
		bool complete = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or m_io_done )
				return ;

			if( m_first == first_completion::none )
				m_first = first_completion::io;

			m_io_error = canonical_error(error);
			m_value.emplace(std::move(value));
			m_io_done = true;

			complete = m_timer_done;
			if( not m_timer_done )
			{
				if( m_launching )
					m_pending_timer_cancellation |= asio::cancellation_type::all;
				else
					cancel_timer = true;
			}
		}
		if( cancel_timer )
		{
			emit_cancellation(asio::cancellation_type::none,
				asio::cancellation_type::all
			);
		}
		if( complete )
			complete_normal();
	}

	void timer_complete(error_code error)
	{
		bool cancel_io = false;
		bool complete = false;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or m_timer_done )
				return ;

			if( m_first == first_completion::none )
				m_first = first_completion::timer;

			m_timer_error = canonical_error(error);
			m_timer_done = true;

			complete = m_io_done;
			if( not m_io_done )
			{
				if( m_launching )
					m_pending_io_cancellation |= asio::cancellation_type::all;
				else
					cancel_io = true;
			}
		}
		if( cancel_io )
		{
			emit_cancellation(asio::cancellation_type::all,
				asio::cancellation_type::none
			);
		}
		if( complete )
			complete_normal();
	}

	void complete_normal()
	{
		std::optional<Value> value;
		error_code error;
		{
			std::lock_guard lock(m_mutex);
			if( m_completed or not m_io_done or not m_timer_done )
				return ;

			m_completed = true;
			if( m_first == first_completion::io )
				error = m_io_error;

			else if( not m_timer_error )
				error = make_error_code(errc::timed_out);
			else
				error = m_io_error ? m_io_error : m_timer_error;

			value.emplace(std::move(*m_value));
		}
		m_caller_slot.clear();
		m_completion_fn(*this, error, std::move(*value), false);
	}

	void fail(const std::exception_ptr &exception)
	{
		{
			std::lock_guard lock(m_mutex);
			if( m_completed )
				return ;

			m_completed = true;
			m_launching = false;
		}
		m_caller_slot.clear();

		emit_cancellation(asio::cancellation_type::all,
			asio::cancellation_type::all
		);
		m_completion_fn(*this, exception_error(exception), Value{}, true);
	}

	void emit_cancellation
	(asio::cancellation_type_t io_type, asio::cancellation_type_t timer_type)
	{
		std::lock_guard emit_lock(m_emit_mutex);
		if( io_type != asio::cancellation_type::none )
			m_io_cancellation.emit(io_type);

		if( timer_type != asio::cancellation_type::none )
			m_timer_cancellation.emit(timer_type);
	}

	Exec m_exec {};
	asio::steady_timer m_timer;

	asio::cancellation_signal m_io_cancellation;
	asio::cancellation_signal m_timer_cancellation;
	asio::cancellation_slot m_caller_slot;
	completion_fn_t m_completion_fn;

	std::mutex m_mutex;
	std::mutex m_emit_mutex;
	std::optional<Value> m_value;

	error_code m_io_error;
	error_code m_timer_error;
	first_completion m_first = first_completion::none;

	asio::cancellation_type_t m_pending_io_cancellation =
		asio::cancellation_type::none;

	asio::cancellation_type_t m_pending_timer_cancellation =
		asio::cancellation_type::none;

	bool m_launching = true;
	bool m_io_done = false;
	bool m_timer_done = false;
	bool m_completed = false;
};

template <typename Value, typename Exec, typename Handler>
class RIWO_CORE_TAPI timed_io_operation final :
	public timed_io_state<Value,Exec>
{
	using self_t = timed_io_operation;
	using base_t = timed_io_state<Value,Exec>;

public:
	timed_io_operation(const Exec &exec, Handler handler, std::chrono::nanoseconds timeout) :
		base_t(exec, asio::get_associated_cancellation_slot(handler), timeout, &self_t::complete),
		m_handler(std::move(handler))
	{}

private:
	static void complete(base_t &base, error_code error, Value value, bool post)
	{
		auto &self = static_cast<self_t&>(base);
		auto handler = std::move(*self.m_handler);
		self.m_handler.reset();
		if( post )
		{
			if constexpr( std::same_as<Value,std::monostate> )
			{
				ignore_unused(value);
				riwo::post_completion(self.executor(),
					std::move(handler), error);
			}
			else
			{
				riwo::post_completion(self.executor(),
					std::move(handler), error, std::move(value));
			}
			return ;
		}

		auto completion_exec = asio::get_associated_executor(handler, self.executor());
		auto allocator = asio::get_associated_allocator(handler);

		auto completion = asio::bind_allocator(allocator,
		[handler = std::move(handler), error, value = std::move(value)]() mutable
		{
			if constexpr( std::same_as<Value,std::monostate> )
			{
				ignore_unused(value);
				std::move(handler)(error);
			}
			else
				std::move(handler)(error, std::move(value));
		});
		asio::dispatch(completion_exec, std::move(completion));
	}

	optional<Handler> m_handler;
};

template <typename Value, typename Exec, typename Initiator, typename Handler>
RIWO_CORE_TAPI void start_timed_io
(const Exec &exec, Initiator initiation, std::chrono::nanoseconds timeout, Handler &&handler)
{
	auto allocator = asio::get_associated_allocator(handler);
	using handler_t = std::remove_cvref_t<Handler>;
	using state_t = timed_io_operation<Value,Exec,handler_t>;

	auto state = std::allocate_shared<state_t>(allocator, exec,
		std::forward<Handler>(handler), timeout
	);
	state->start(std::move(initiation));
}

template <typename Value, typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_io_timed
(const Exec &exec, Initiator initiation, std::chrono::nanoseconds timeout, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code,Value)>(
	[exec, operation = std::move(initiation), timeout](auto completion_handler) mutable
	{
		start_timed_io<Value>(exec,
			std::move(operation), timeout, std::move(completion_handler)
		);
	},
	completion_token);
}

template <typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI auto initiate_io_timed_void
(const Exec &exec, Initiator initiation, std::chrono::nanoseconds timeout, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code)>(
	[exec, operation = std::move(initiation), timeout](auto completion_handler) mutable
	{
		start_timed_io<std::monostate>(exec,
			std::move(operation), timeout, std::move(completion_handler)
		);
	}, completion_token);
}

template <typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_CORE_TAPI
auto initiate_io_direct_void(const Exec &exec, Initiator initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code)>(
	[exec, start = std::move(initiation)]<typename Handler>(Handler completion_handler) mutable
	{
		start(direct_io_void_handler<Handler,Exec>(
			std::move(completion_handler), exec)
		);
	},
	completion_token);
}

} //namespace detail

template <concepts::exec Exec, typename Handler, typename...Args>
void post_completion(const Exec &exec, Handler &&handler, Args&&...args)
{
	auto immediate_exec = asio::get_associated_immediate_executor(handler, exec);
	auto allocator = asio::get_associated_allocator(handler);

	auto completion = asio::bind_allocator(allocator,
	[completion = std::forward<Handler>(handler), ...values = std::forward<Args>(args)]() mutable {
		std::move(completion)(std::move(values)...);
	});
	if constexpr( asio::execution::is_executor<decltype(immediate_exec)>::value )
	{
		asio::prefer(immediate_exec, asio::execution::allocator(allocator))
			.execute(std::move(completion));
	}
	else
		immediate_exec.dispatch(std::move(completion), allocator);
}

template <typename Buffer, typename Source>
Buffer copy_buffer_data(Source &&source) requires
(is_buffer_v<Buffer> and is_buffer_v<std::remove_cvref_t<Source>> and not is_array_buffer_v<Buffer>)
{
	using source_t = std::remove_cvref_t<Source>;
	if constexpr( std::same_as<Buffer,source_t> )
		return std::forward<Source>(source);
	else
	{
		using value_t = Buffer::value_type;
		const auto byte_size = source.size() *
			sizeof(typename source_t::value_type);

		const auto value_size = byte_size / sizeof(value_t) +
			static_cast<size_t>(byte_size % sizeof(value_t) != 0);

		Buffer result {};
		result.resize(value_size);

		if( byte_size > 0 )
			std::memcpy(result.data(), source.data(), byte_size);
		return result;
	}
}

template <typename Value>
[[nodiscard]] Value expected_value_or_throw(sys_expected<Value> expected)
{
	if( not expected )
		system_error::loc_throw(detail::canonical_error(expected.error()));
	return std::move(*expected);
}

template <typename Value, typename Error>
[[nodiscard]] Value expected_value_or_error(sys_expected<Value> expected, Error &error)
	noexcept(std::is_nothrow_move_constructible_v<Value>)
	requires is_error_code_token_v<Error&>
{
	if( not expected )
	{
		error = detail::canonical_error(expected.error());
		return {};
	}
	error.clear();
	return std::move(*expected);
}

template <typename T>
[[nodiscard]] auto capture_async_argument(T &&argument)
{
	if constexpr( std::is_lvalue_reference_v<T> )
		return std::ref(argument);
	else
		return std::remove_cvref_t<T>(std::forward<T>(argument));
}

template <typename T>
[[nodiscard]] decltype(auto) unwrap_async_argument(T &argument) noexcept
{
	if constexpr( detail::is_async_argument_reference<std::remove_cvref_t<T>>::value )
		return argument.get();
	else
		return (argument);
}

inline error_code exception_error(const std::exception_ptr &exception) noexcept
{
	if( not exception )
		return {};
	try {
		std::rethrow_exception(exception);
	}
	catch(const std::system_error &ex) {
		return detail::canonical_error(ex.code());
	}
	catch(const std::bad_alloc&) {
		return make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {}
	return make_system_error_code(std::errc::io_error);
}

template <typename Value, concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] auto initiate_expected(const Exec &exec, Factory factory, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		return detail::initiate_expected<true,Value>(exec, std::move(factory),
			std::chrono::duration_cast<std::chrono::nanoseconds>(timed_token.time),
			std::move(timed_token.token)
		);
	}
	else
	{
		return detail::initiate_expected<false,Value>(exec, std::move(factory),
			std::chrono::nanoseconds::zero(), std::forward<Token>(token)
		);
	}
}

template <typename Value, concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] auto initiate_preserved_expected(const Exec &exec, Factory factory, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		return detail::initiate_preserved_expected<true,Value>(exec, std::move(factory),
			std::chrono::duration_cast<std::chrono::nanoseconds>(timed_token.time),
			std::move(timed_token.token)
		);
	}
	else
	{
		return detail::initiate_preserved_expected<false,Value>(exec, std::move(factory),
			std::chrono::nanoseconds::zero(), std::forward<Token>(token)
		);
	}
}

template <concepts::exec Exec, typename Factory, typename Token>
[[nodiscard]] auto initiate_expected_void(const Exec &exec, Factory factory, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		return detail::initiate_expected_void<true>(exec, std::move(factory),
			std::chrono::duration_cast<std::chrono::nanoseconds>(timed_token.time),
			std::move(timed_token.token)
		);
	}
	else
	{
		return detail::initiate_expected_void<false>(exec, std::move(factory),
			std::chrono::nanoseconds::zero(), std::forward<Token>(token)
		);
	}
}

template <typename Value, concepts::exec Exec, typename Initiator, typename Token>
[[nodiscard]] auto initiate_io(const Exec &exec, Initiator initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		if( timed_token.time <= milliseconds::zero() )
		{
			return detail::initiate_io_direct<Value>(exec, std::move(initiation),
				std::move(timed_token.token)
			);
		}
		return detail::initiate_io_timed<Value>(exec, std::move(initiation),
			std::chrono::duration_cast<std::chrono::nanoseconds>(timed_token.time),
			std::move(timed_token.token)
		);
	}
	else
	{
		return detail::initiate_io_direct<Value>(exec, std::move(initiation),
			std::forward<Token>(token)
		);
	}
}

template <concepts::exec Exec, typename Initiator, typename Token>
[[nodiscard]] auto initiate_io_void(const Exec &exec, Initiator initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		if( timed_token.time <= milliseconds::zero() )
		{
			return detail::initiate_io_direct_void(exec, std::move(initiation),
				std::move(timed_token.token)
			);
		}
		return detail::initiate_io_timed_void(exec, std::move(initiation),
			std::chrono::duration_cast<std::chrono::nanoseconds>(timed_token.time),
			std::move(timed_token.token)
		);
	}
	else
	{
		return detail::initiate_io_direct_void(exec, std::move(initiation),
			std::forward<Token>(token)
		);
	}
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_ASYNC_EXPECTED_H
