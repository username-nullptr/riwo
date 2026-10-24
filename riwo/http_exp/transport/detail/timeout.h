// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_TIMEOUT_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_TIMEOUT_H

#include <riwo/http_exp/transport/connection.h>
#include <riwo/core/cxx/asio.h>

namespace riwo::http_exp::detail
{

#if defined(_WIN32)
using native_socket_timeout_t = DWORD;
#else
using native_socket_timeout_t = timeval;
#endif

[[nodiscard]] inline native_socket_timeout_t native_socket_timeout(
	connection::timeout_t value) noexcept
{
	const auto count = std::max<int64_t>(0, value.count());
#if defined(_WIN32)
	return static_cast<DWORD>(count);
#else
	native_socket_timeout_t result {};
	result.tv_sec = static_cast<time_t>(count / 1000);
	result.tv_usec = static_cast<suseconds_t>((count % 1000) * 1000);
	return result;
#endif
}

inline void apply_socket_timeout(asio::ip::tcp::socket &socket, int option,
	optional<connection::timeout_t> value) noexcept
{
	const auto timeout = value ? native_socket_timeout(*value) :
		native_socket_timeout_t{};
	(void)::setsockopt(socket.native_handle(), SOL_SOCKET, option,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
}

[[nodiscard]] inline bool socket_timeout_error(
	const error_code &error) noexcept
{
	return error == asio::error::make_error_code(asio::error::would_block) or
		error == asio::error::make_error_code(asio::error::try_again);
}

template <typename Owner, typename Initiation>
void start_timed_io(std::shared_ptr<Owner> owner,
	asio::any_io_executor executor, connection::timeout_t timeout,
	connection::io_handler_t handler, Initiation initiation)
{
	struct timeout_state
	{
		explicit timeout_state(asio::any_io_executor value) :
			timer(std::move(value))
		{
		}

		asio::steady_timer timer;
		std::atomic<bool> completed {false};
		std::atomic<bool> timed_out {false};
	};

	auto state = std::make_shared<timeout_state>(executor);
	state->timer.expires_after(timeout);
	state->timer.async_wait([owner, state](error_code error)
	{
		if( error or state->completed.exchange(true,
			std::memory_order_acq_rel) )
			return ;
		state->timed_out.store(true, std::memory_order_release);
		owner->cancel();
	});

	auto completion_executor = asio::get_associated_executor(handler, executor);
	auto completion_allocator = handler.get_allocator();
	auto cancellation_slot = handler.get_cancellation_slot();
	auto completion = [owner = std::move(owner), state,
		handler = std::move(handler)](error_code error, size_t size) mutable
	{
		if( not state->completed.exchange(true, std::memory_order_acq_rel) )
			(void)state->timer.cancel();
		if( state->timed_out.load(std::memory_order_acquire) and
			error == asio::error::make_error_code(
				asio::error::operation_aborted) )
			error = asio::error::make_error_code(asio::error::timed_out);
		std::move(handler)(error, size);
	};

	try {
		initiation(asio::bind_executor(std::move(completion_executor),
			asio::bind_allocator(std::move(completion_allocator),
			asio::bind_cancellation_slot(std::move(cancellation_slot),
				std::move(completion)))));
	}
	catch(...)
	{
		state->completed.store(true, std::memory_order_release);
		(void)state->timer.cancel();
		throw;
	}
}

template <typename Operation, typename Cancel>
[[nodiscard]] size_t run_timed_sync_io(optional<connection::timeout_t> timeout,
	Operation operation, Cancel cancel, error_code &error)
{
	struct timeout_state
	{
		std::mutex mutex;
		std::condition_variable signal;
		bool completed = false;
		bool timed_out = false;
	};

	timeout_state state;
	std::jthread watchdog;
	if( timeout )
	{
		watchdog = std::jthread([&state, timeout = *timeout,
			cancel = std::move(cancel)]() mutable
		{
			std::unique_lock lock(state.mutex);
			if( state.signal.wait_for(lock, timeout,
				[&state] { return state.completed; }) )
				return ;
			state.timed_out = true;
			lock.unlock();
			cancel();
		});
	}

	auto finish = [&state, &watchdog]
	{
		{
			std::lock_guard lock(state.mutex);
			state.completed = true;
		}
		state.signal.notify_one();
		if( watchdog.joinable() )
			watchdog.join();
	};

	size_t size = 0;
	try {
		size = operation();
	}
	catch(...)
	{
		finish();
		throw;
	}
	finish();
	if( state.timed_out )
		error = asio::error::make_error_code(asio::error::timed_out);
	else if( socket_timeout_error(error) )
		error = asio::error::make_error_code(asio::error::timed_out);
	return size;
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_TIMEOUT_H
