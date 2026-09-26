// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_HANDSHAKE_IO_H
#define RIWO_WEBSOCKET_DETAIL_HANDSHAKE_IO_H

#include <riwo/core/async_expected.h>
#include <riwo/websocket/cxx/attributes.h>
#include <riwo/websocket/cxx/concepts.h>
#include <riwo/websocket/types.h>

namespace riwo::websocket::detail
{

[[nodiscard]] constexpr std::chrono::nanoseconds
effective_handshake_timeout(std::chrono::milliseconds timeout) noexcept
{
	return timeout > std::chrono::milliseconds::zero() ?
		   std::chrono::duration_cast<std::chrono::nanoseconds>(timeout) :
		   std::chrono::nanoseconds(1);
}

template <typename Value, typename Exec>
class RIWO_WEBSOCKET_TAPI handshake_io_state :
	public std::enable_shared_from_this<handshake_io_state<Value,Exec>>
{
	using self_t = handshake_io_state;
	using complete_fn_t = void (*)(self_t&, error_code, Value);
	using fail_fn_t = void (*)(self_t&, error_code);

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
	handshake_io_state(const Exec &exec, asio::cancellation_slot caller_slot,
		const optional<stream_config> &fallback_config, std::chrono::nanoseconds timeout,
		complete_fn_t complete_fn, fail_fn_t fail_fn) :
		m_exec(exec), m_fallback_config(fallback_config), m_timer(exec),
		m_caller_slot(caller_slot), m_complete_fn(complete_fn),
		m_fail_fn(fail_fn)
	{
		m_timer.expires_after(timeout);
	}

	[[nodiscard]] io_handler begin_start()
	{
		auto self = this->shared_from_this();
		if( m_caller_slot.is_connected() )
		{
			m_caller_slot.emplace<caller_cancellation_handler>(
				std::weak_ptr<self_t>(self)
			);
		}
		return io_handler{std::move(self)};
	}

	void finish_start()
	{
		auto self = this->shared_from_this();
		m_timer.async_wait(timer_handler{std::move(self)});
		finish_launch();
	}

	void fail_start(const std::exception_ptr &exception) {
		fail(exception);
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

			m_io_error = riwo::detail::canonical_error(error);
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

			m_timer_error = riwo::detail::canonical_error(error);
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
				error = asio::error::timed_out;
			else
				error = m_io_error ? m_io_error : m_timer_error;

			value.emplace(std::move(*m_value));
		}
		m_caller_slot.clear();
		m_complete_fn(*this, error, std::move(*value));
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
		const auto error = exception_error(exception);
		m_fail_fn(*this, error);
	}

protected:
	[[nodiscard]] Value make_fallback()
	{
		if constexpr( std::constructible_from<Value,const Exec&,stream_config> )
		{
			return Value(m_exec,
				m_fallback_config.value_or(stream_config{})
			);
		}
		else
			return Value(m_exec);
	}

	[[nodiscard]] const Exec& executor() const noexcept {
		return m_exec;
	}

private:
	void emit_cancellation(asio::cancellation_type_t io_type, asio::cancellation_type_t timer_type)
	{
		std::lock_guard emit_lock(m_emit_mutex);
		if( io_type != asio::cancellation_type::none )
			m_io_cancellation.emit(io_type);

		if( timer_type != asio::cancellation_type::none )
			m_timer_cancellation.emit(timer_type);
	}

	Exec m_exec {};
	optional<stream_config> m_fallback_config;

	asio::steady_timer m_timer;
	asio::cancellation_signal m_io_cancellation;
	asio::cancellation_signal m_timer_cancellation;
	asio::cancellation_slot m_caller_slot;

	complete_fn_t m_complete_fn;
	fail_fn_t m_fail_fn;

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
class RIWO_CORE_TAPI handshake_io_operation final : public handshake_io_state<Value,Exec>
{
	using self_t = handshake_io_operation;
	using base_t = handshake_io_state<Value,Exec>;

public:
	handshake_io_operation(const Exec &exec, Handler handler,
		optional<stream_config> fallback_config, std::chrono::nanoseconds timeout) :
		base_t(exec, asio::get_associated_cancellation_slot(handler),
			std::move(fallback_config), timeout, &self_t::complete, &self_t::fail
		),
		m_handler(std::move(handler)) {}

private:
	static void complete(base_t &base, error_code error, Value value)
	{
		auto &self = static_cast<self_t&>(base);
		auto handler = std::move(*self.m_handler);

		self.m_handler.reset();
		auto completion_exec = asio::get_associated_executor(handler, self.executor());
		auto allocator = asio::get_associated_allocator(handler);

		asio::dispatch(completion_exec, asio::bind_allocator(allocator,
		[handler = std::move(handler), value = std::move(value), error]() mutable {
			std::move(handler)(error, std::move(value));
		}));
	}

	static void fail(base_t &base, error_code error)
	{
		auto &self = static_cast<self_t&>(base);
		auto handler = std::move(*self.m_handler);

		self.m_handler.reset();
		riwo::post_completion(self.executor(),
			std::move(handler), error, self.make_fallback()
		);
	}

private:
	optional<Handler> m_handler;
};

template <typename Value, typename Exec, typename Initiator, typename Handler>
void start_handshake_io(const Exec &exec, Initiator initiation,
	std::chrono::nanoseconds timeout, optional<stream_config> fallback_config, Handler &&handler)
{
	auto allocator = asio::get_associated_allocator(handler);
	using handler_t = std::remove_cvref_t<Handler>;
	using state_t = handshake_io_operation<Value,Exec,handler_t>;

	auto state = std::allocate_shared<state_t>(allocator, exec,
		std::forward<Handler>(handler), std::move(fallback_config), timeout
	);
	try {
		auto io_handler = state->begin_start();
		initiation(std::move(io_handler));
		state->finish_start();
	}
	catch(...) {
		state->fail_start(std::current_exception());
	}
}

template <typename Value, typename Exec, typename Initiator, typename Token>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto initiate_handshake_io(const Exec &exec, Initiator initiation,
	std::chrono::milliseconds timeout, optional<stream_config> fallback_config, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));
	const auto effective_timeout = effective_handshake_timeout(timeout);

	return asio::async_initiate<token_t,void(error_code,Value)>([exec,
		operation = std::move(initiation), effective_timeout, fallback_config = fallback_config
	](auto completion_handler) mutable
	{
		start_handshake_io<Value>(exec, std::move(operation), effective_timeout,
			std::move(fallback_config), std::move(completion_handler)
		);
	},
	completion_token);
}

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_HANDSHAKE_IO_H
