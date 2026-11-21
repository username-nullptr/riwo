// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_DETAIL_DIRECTORY_WATCHER_H
#define RIWO_SYSTEM_DETAIL_DIRECTORY_WATCHER_H

#include <riwo/core/asio/buffer.h>
#include <condition_variable>
#include <map>

namespace riwo::system { namespace detail
{

using entry_metadata_t = std::array<std::uintmax_t,8>;
using directory_snapshot_t = std::map<std::filesystem::path,entry_metadata_t>;

[[nodiscard]] RIWO_SYSTEM_API sys_expected<directory_snapshot_t>
scan_directory(const std::filesystem::path &directory);

[[nodiscard]] RIWO_SYSTEM_API std::vector<directory_change>
compare_directory_snapshots(const directory_snapshot_t &previous, const directory_snapshot_t &current);

template <concepts::time_p Time>
[[nodiscard]] std::chrono::steady_clock::time_point watcher_deadline(const Time &timeout)
{
	using clock = std::chrono::steady_clock;
	using duration_t = std::chrono::duration<long double>;

	const auto now = clock::now();
	constexpr auto latest = clock::time_point::max();

	duration_t remaining;
	if constexpr( concepts::duration_p<Time> )
	{
		if( timeout == Time::zero() )
			return latest;
		remaining = timeout;
	}
	else if constexpr( std::same_as<typename Time::clock,clock> )
		remaining = duration_t(timeout.time_since_epoch()) - duration_t(now.time_since_epoch());
	else
		remaining = duration_t(timeout.time_since_epoch()) - duration_t(Time::clock::now().time_since_epoch());

	if( not (remaining > duration_t::zero()) )
		return now;

	else if( remaining >= latest - now )
		return latest;

	return now + std::chrono::ceil<clock::duration>(remaining);
}

} //namespace detail

template <concepts::exec Exec>
class RIWO_SYSTEM_TAPI directory_watcher<Exec>::impl
{
public:
	struct pending_wait
	{
		uint64_t generation;
		bool asynchronous;
	};

	struct wait_guard
	{
		impl &state;

		~wait_guard()
		{
			std::lock_guard lock(state.mutex);
			state.active_wait.reset();
		}
	};

	impl(const strand_t &strand, std::filesystem::path path,
		interval_t poll_interval, detail::directory_snapshot_t initial) :
		timer(strand), directory(std::move(path)),
		interval(poll_interval), snapshot(std::move(initial)) {}

public:
	static void cancel_wait(std::shared_ptr<impl> self, const strand_t &strand, bool closing) noexcept
	{
		if( not self )
			return ;

		bool cancel_timer;
		{
			std::lock_guard lock(self->mutex);
			++self->generation;
			self->closed |= closing;
			cancel_timer = self->active_wait and self->active_wait->asynchronous;
		}
		self->wake.notify_all();
		if( not cancel_timer )
			return ;
		try {
			asio::dispatch(strand, [state = std::move(self)]
			{
				std::lock_guard lock(state->mutex);
				if( state->active_wait and state->active_wait->asynchronous and
					state->cancelled(state->active_wait->generation) )
					state->timer.cancel();
			});
		}
		catch(...) {}
	}

	[[nodiscard]] uint64_t get_generation() noexcept
	{
		std::lock_guard lock(mutex);
		return generation;
	}

	[[nodiscard]] sys_expected<changes_t> wait_sync(clock_t::time_point deadline) noexcept
	try {
		const auto ticket = get_generation();
		if( const auto error = begin_wait(ticket, false) )
			return sys_unexpected(error);

		wait_guard guard {*this};
		auto changes = poll(ticket);

		while( changes and changes->empty() )
		{
			const auto now = clock_t::now();
			if( now >= deadline )
				return sys_unexpected(make_system_error_code(std::errc::timed_out));

			if( std::unique_lock lock(mutex);
				wake.wait_until(lock, std::min(deadline, next_poll(now)),
				[&] { return cancelled(ticket); }) )
				return sys_unexpected(make_error_code(asio::error::operation_aborted));

			changes = poll(ticket);
		}
		return changes;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}

	// The coroutine frame owns the state from creation through completion.
	[[nodiscard]] static awaitable<sys_expected<changes_t>>
	co_wait(std::shared_ptr<impl> self, uint64_t ticket)
	{
		if( not self )
			co_return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		auto &state = *self;
		if( const auto error = state.begin_wait(ticket, true) )
			co_return sys_unexpected(error);

		wait_guard guard {state};
		auto cancellation = co_await asio::this_coro::cancellation_state;
		auto changes = state.poll(ticket, cancellation);

		while( changes and changes->empty() )
		{
			state.timer.expires_at(state.next_poll(clock_t::now()));
			error_code error {};

			co_await state.timer.async_wait(asio::redirect_error(use_awaitable, error));
			if( error )
				co_return sys_unexpected(error);

			changes = state.poll(ticket, cancellation);
		}
		co_return changes;
	}

	template <bool Timed, typename Token>
	[[nodiscard]] static auto wait_async
	(std::shared_ptr<impl> self, const strand_t &strand, Token &&token, std::chrono::nanoseconds timeout = {})
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));

		return asio::async_initiate<token_t,void(error_code,changes_t)>(
		[exec = asio::any_io_executor(strand), self = std::move(self), timeout]
		(auto handler) mutable
		{
			// Lazy tokens become pending only when they are actually initiated.
			const auto generation = self ? self->get_generation() : 0;

			riwo::detail::initiate_expected<Timed,changes_t>(exec,
			[self = std::move(self), generation]() mutable {
				return co_wait(std::move(self), generation);
			},
			timeout, std::move(handler));
		},
		completion_token);
	}

public:
	// Call with mutex held.
	[[nodiscard]] bool cancelled(uint64_t ticket) const noexcept {
		return closed or generation != ticket;
	}

	[[nodiscard]] error_code begin_wait(uint64_t ticket, bool asynchronous)
	{
		std::lock_guard lock(mutex);
		if( cancelled(ticket) )
			return make_error_code(asio::error::operation_aborted);

		if( active_wait )
			return make_system_error_code(std::errc::device_or_resource_busy);

		active_wait = pending_wait {
			.generation = ticket, .asynchronous = asynchronous
		};
		return {};
	}

	[[nodiscard]] clock_t::time_point next_poll(clock_t::time_point now) const noexcept {
		return now + std::min(interval, clock_t::time_point::max() - now);
	}

	[[nodiscard]] sys_expected<changes_t> poll
	(uint64_t ticket, const asio::cancellation_state &cancellation = {})
	{
		const auto aborted = make_error_code(asio::error::operation_aborted);
		const auto stopped = [&] {
			return cancelled(ticket) or cancellation.cancelled() != asio::cancellation_type::none;
		};
		{
			std::lock_guard lock(mutex);
			if( stopped() )
				return sys_unexpected(aborted);
		}
		auto current = detail::scan_directory(directory);
		if( not current )
			return sys_unexpected(current.error());

		// The active wait owns the snapshot for both synchronous and asynchronous modes.
		auto changes = detail::compare_directory_snapshots(snapshot, *current);
		std::lock_guard lock(mutex);

		if( stopped() )
			return sys_unexpected(aborted);

		if( not changes.empty() )
			snapshot = std::move(*current);
		return changes;
	}

public:
	asio::basic_waitable_timer <
		clock_t, asio::wait_traits<clock_t>, strand_t
	> timer;

	std::filesystem::path directory {};
	interval_t interval {};

	detail::directory_snapshot_t snapshot {};
	std::mutex mutex {};

	std::condition_variable wake {};
	optional<pending_wait> active_wait {};

	uint64_t generation = 0;
	bool closed = false;
};

template <concepts::exec Exec>
directory_watcher<Exec>::directory_watcher
(const strand_t &strand, std::shared_ptr<impl> implementation) noexcept :
	m_strand(strand), m_impl(std::move(implementation))
{

}

template <concepts::exec Exec>
directory_watcher<Exec>::directory_watcher(directory_watcher &&other) noexcept :
	m_strand(other.m_strand), m_impl(std::move(other.m_impl))
{

}

template <concepts::exec Exec>
auto directory_watcher<Exec>::operator=(directory_watcher &&other) noexcept -> directory_watcher&
{
	if( this != &other )
	{
		impl::cancel_wait(m_impl, m_strand, true);
		m_strand = other.m_strand;
		m_impl = std::move(other.m_impl);
	}
	return *this;
}

template <concepts::exec Exec>
directory_watcher<Exec>::~directory_watcher()
{
	impl::cancel_wait(m_impl, m_strand, true);
}

template <concepts::exec Exec>
template <concepts::sched Scheduler>
auto directory_watcher<Exec>::open
(Scheduler &&scheduler, const path_t &directory, interval_t interval) -> sys_expected<directory_watcher>
	requires concepts::match_sched<Scheduler,executor_t>
{
	namespace fs = std::filesystem;

	const executor_t executor(get_executor_helper(std::forward<Scheduler>(scheduler)));
	const auto invalid = make_system_error_code(std::errc::invalid_argument);

	if constexpr( requires { static_cast<bool>(executor); } )
	{
		if( not static_cast<bool>(executor) )
			return sys_unexpected(invalid);
	}
	if( directory.empty() or interval <= interval_t::zero() or
		directory.native().find(fs::path::value_type{}) != fs::path::string_type::npos )
		return sys_unexpected(invalid);

	error_code error {};
	auto path = fs::absolute(directory, error);

	if( error )
		return sys_unexpected(error);

	auto snapshot = detail::scan_directory(path);
	if( not snapshot )
		return sys_unexpected(snapshot.error());

	auto strand = asio::make_strand(executor);
	return directory_watcher(strand, std::make_shared<impl>(
		strand, std::move(path), interval, std::move(*snapshot)
	));
}

template <concepts::exec Exec>
auto directory_watcher<Exec>::open
(const path_t &directory, interval_t interval) -> sys_expected<directory_watcher>
	requires concepts::match_sched<io_executor_t,executor_t>
{
	return open(riwo::get_executor(), directory, interval);
}

template <concepts::exec Exec>
auto directory_watcher<Exec>::get_executor() const noexcept -> executor_t
{
	return m_strand.get_inner_executor();
}

template <concepts::exec Exec>
template <typename Token>
auto directory_watcher<Exec>::wait(Token &&token) requires
	concepts::tf_opt_token<Token,error_code,changes_t> or concepts::time_p<Token>
{
	if constexpr( is_error_code_token_v<Token> or is_sync_opt_token_v<Token> or concepts::time_p<Token> )
	{
		const auto state = m_impl;
		auto deadline = clock_t::time_point::max();
		if constexpr( concepts::time_p<Token> )
			deadline = detail::watcher_deadline(token);

		auto result = state ? state->wait_sync(deadline) :
			sys_expected<changes_t>(sys_unexpected(make_system_error_code(std::errc::invalid_argument)));

		if constexpr( is_error_code_token_v<Token> )
			return expected_value_or_error(std::move(result), token);
		else if constexpr( concepts::as_expected_p<Token> )
		{
			riwo::detail::canonicalize_expected(result);
			return result;
		}
		else if constexpr( concepts::as_tuple_p<Token> )
		{
			error_code error {};
			auto changes = expected_value_or_error(std::move(result), error);
			return std::tuple(error, std::move(changes));
		}
		else
			return expected_value_or_throw(std::move(result));
	}
	else if constexpr( is_redirect_time_v<std::remove_cvref_t<Token>> )
	{
		auto timed = std::forward<Token>(token);
		return impl::template wait_async<true>(m_impl, m_strand, std::move(timed.token), timed.time);
	}
	else
		return impl::template wait_async<false>(m_impl, m_strand, std::forward<Token>(token));
}

template <concepts::exec Exec>
void directory_watcher<Exec>::cancel() noexcept
{
	impl::cancel_wait(m_impl, m_strand, false);
}

} //namespace riwo::system


#endif //RIWO_SYSTEM_DETAIL_DIRECTORY_WATCHER_H
