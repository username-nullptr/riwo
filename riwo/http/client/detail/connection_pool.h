// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_CONNECTION_POOL_H
#define RIWO_HTTP_CLIENT_DETAIL_CONNECTION_POOL_H

#include <riwo/core/async_expected.h>

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_connection_pool<Exec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	using clock_t = std::chrono::steady_clock;
	using timer_t = asio::basic_waitable_timer <
		clock_t, asio::wait_traits<clock_t>, executor_t
	>;
	struct key_hash
	{
		[[nodiscard]] size_t operator()(const target_t &key) const noexcept
		{
			auto seed = std::hash<std::string>{}(key.host);

			seed ^= std::hash<uint16_t>{}(key.port) + 0x9e3779b9U +
				(seed << 6U) + (seed >> 2U);

			seed ^= std::hash<unsigned>{}(static_cast<unsigned>(key.security))
				+ 0x9e3779b9U + (seed << 6U) + (seed >> 2U);

			seed ^= std::hash<bool>{}(key.no_delay) + 0x9e3779b9U +
				(seed << 6U) + (seed >> 2U);

			return seed;
		}
	};

	struct idle_connection
	{
		connection_ptr connection {};
		clock_t::time_point idle_since {};
		target_t key {};
	};
	using idle_list = std::list<idle_connection>;
	using idle_iterator = idle_list::iterator;

	struct connection_bucket
	{
		std::deque<idle_iterator> idle {};
		size_t connecting = 0;
		size_t total = 0;
	};

	enum class wake_reason {
		none, state_changed, cancelled, stopped
	};

	struct waiter;
	using waiter_list = std::list<std::shared_ptr<waiter>>;

	struct waiter
	{
		waiter(executor_t exec, target_t target) :
			timer(std::move(exec)), key(std::move(target))
		{
			timer.expires_at(clock_t::time_point::max());
		}
		timer_t timer {};
		target_t key {};
		wake_reason reason = wake_reason::none;

		waiter_list::iterator all_position {};
		waiter_list::iterator key_position {};
		bool queued = false;
	};

public:
	impl(connector_ptr connector_instance, const config_t &config) :
		m_connector(std::move(connector_instance)), m_config(config),
		m_exec(connector_executor(m_connector)) {}

	impl(executor_t exec, const config_t &config) :
		impl(std::make_shared<connector_t>(std::move(exec)), config) {}

	~impl() {
		invalidate();
	}

public:
	[[nodiscard]] sys_expected<lease_ptr>
	acquire(const target_t &key, bool wait_when_full) noexcept
	{
		try {
			size_t request_generation = 0;
			{
				std::lock_guard lock(m_mutex);
				request_generation = m_cancel_generation;
			}
			for(;;)
			{
				connection_ptr conn {};
				size_t generation = 0;
				{
					std::unique_lock lock(m_mutex);
					if( m_stopped )
						return sys_unexpected(make_error_code(errc::operation_aborted));

					if( request_generation != m_cancel_generation )
						return sys_unexpected(make_error_code(errc::operation_aborted));

					if( m_config.max_count == 0 )
						return sys_unexpected(make_system_error_code(std::errc::no_buffer_space));

					conn = take_idle_locked(key);
					if( conn )
					{
						lock.unlock();
						return lease_from_reserved(key, std::move(conn));
					}
					if( m_total_count >= m_config.max_count )
						evict_one_idle_locked();

					if( m_total_count < m_config.max_count )
					{
						auto &bucket = m_buckets[key];
						++bucket.connecting;
						++bucket.total;
						++m_total_count;
						generation = request_generation;
					}
					else if( not wait_when_full )
					{
						return sys_unexpected(make_error_code(
							std::errc::resource_unavailable_try_again
						));
					}
					else
					{
						auto epoch = m_state_epoch;
						m_condition.wait(lock, [&] {
							return m_stopped or m_state_epoch != epoch;
						});
						continue;
					}
				}
				error_code error {};
				conn = m_connector->connect(key, error);

				return finish_connect(key, generation,
					std::move(conn), error
				);
			}
		}
		catch(const std::bad_alloc&) {
			return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
		}
		catch(const std::system_error &ex) {
			return sys_unexpected(ex.code());
		}
		catch(...) {}
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}

	[[nodiscard]] awaitable<sys_expected<lease_ptr>>
	co_acquire(target_t key, bool wait_when_full) noexcept
	{
		size_t request_generation = 0;
		{
			std::lock_guard lock(m_mutex);
			request_generation = m_cancel_generation;
		}
		for(;;)
		{
			connection_ptr conn {};
			size_t generation = 0;
			std::shared_ptr<waiter> current_waiter {};
			{
				std::unique_lock lock(m_mutex);
				if( m_stopped )
					co_return sys_unexpected(make_error_code(errc::operation_aborted));

				if( request_generation != m_cancel_generation )
					co_return sys_unexpected(make_error_code(errc::operation_aborted));

				if( m_config.max_count == 0 )
					co_return sys_unexpected(make_system_error_code(std::errc::no_buffer_space));

				conn = take_idle_locked(key);
				if( conn )
				{
					lock.unlock();
					co_return lease_from_reserved(key, std::move(conn));
				}
				if( m_total_count >= m_config.max_count )
					evict_one_idle_locked();

				if( m_total_count < m_config.max_count )
				{
					auto &bucket = m_buckets[key];
					++bucket.connecting;
					++bucket.total;
					++m_total_count;
					generation = request_generation;
				}
				else if( not wait_when_full )
				{
					co_return sys_unexpected(make_error_code (
						std::errc::resource_unavailable_try_again
					));
				}
				else
				{
					current_waiter = std::make_shared<waiter>(m_exec, key);
					if( not enqueue_waiter_locked(current_waiter) )
					{
						co_return sys_unexpected(make_error_code (
							std::errc::not_enough_memory
						));
					}
				}
			}
			if( current_waiter )
			{
				error_code wait_error {};
				co_await current_waiter->timer.async_wait (
					asio::redirect_error(use_awaitable, wait_error)
				);
				wake_reason reason = wake_reason::none;
				{
					std::lock_guard lock(m_mutex);
					reason = current_waiter->reason;
					remove_waiter_locked(current_waiter.get());
				}
				if( reason == wake_reason::state_changed )
					continue;
				if( reason == wake_reason::stopped )
					co_return sys_unexpected(make_error_code(errc::operation_aborted));

				if( reason == wake_reason::cancelled or wait_error )
				{
					co_return sys_unexpected(wait_error ? wait_error :
						make_error_code(errc::operation_aborted));
				}
				continue;
			}
			error_code error {};
			conn = co_await m_connector->connect (
				key, asio::redirect_error(use_awaitable, error)
			);
			co_return finish_connect(key, generation,
				std::move(conn), error
			);
		}
		co_return sys_unexpected(make_system_error_code(std::errc::io_error));
	}

public:
	void cancel() noexcept
	{
		std::lock_guard lock(m_mutex);
		++m_cancel_generation;

		for(auto &item : m_waiters)
		{
			item->queued = false;
			wake_waiter_locked(item, wake_reason::cancelled);
		}
		m_waiters.clear();
		m_waiters_by_key.clear();
		state_changed_locked();
	}

	void invalidate() noexcept
	{
		std::lock_guard lock(m_mutex);
		if( m_stopped )
			return ;

		m_stopped = true;
		++m_cancel_generation;

		for(auto &item : m_waiters)
		{
			item->queued = false;
			wake_waiter_locked(item, wake_reason::stopped);
		}
		m_waiters.clear();
		m_waiters_by_key.clear();

		for(auto &[key, bucket] : m_buckets)
		{
			ignore_unused(key);
			while( not bucket.idle.empty() )
			{
				auto idle = bucket.idle.front();
				auto conn = std::move(idle->connection);

				bucket.idle.pop_front();
				m_idle_lru.erase(idle);

				if( bucket.total > 0 )
					--bucket.total;

				if( m_total_count > 0 )
					--m_total_count;

				if( conn )
					ignore_unused(conn->close());
			}
		}
		state_changed_locked();
	}

	[[nodiscard]] size_t count() const noexcept
	{
		std::lock_guard lock(m_mutex);
		return m_total_count;
	}

private:
	[[nodiscard]] static executor_t connector_executor(const connector_ptr &connector_instance)
	{
		if( not connector_instance )
			invalid_argument::loc_throw("connection pool connector is null");
		return connector_instance->get_executor();
	}

	[[nodiscard]] bool reusable
	(const connection_ptr &conn, clock_t::time_point idle_since = {}) const noexcept
	{
		if( not conn or not conn->is_open() )
			return false;

		if( idle_since != clock_t::time_point{} and
			m_config.timeout.idle > config_t::seconds_t::zero() and
			clock_t::now() - idle_since >= m_config.timeout.idle )
			return false;

		// Conservative policy: only an open connection with no readable data and
		// no detected peer close is eligible for reuse. Probe failures and
		// indeterminate states are discarded.
		auto state = conn->probe();
		return state and *state == connection_probe_state::no_event;
	}

	[[nodiscard]] connection_ptr take_idle_locked(const target_t &key) noexcept
	{
		auto pos = m_buckets.find(key);
		if( pos == m_buckets.end() )
			return {};

		while( not pos->second.idle.empty() )
		{
			auto idle_pos = pos->second.idle.back();
			auto idle = std::move(*idle_pos);

			pos->second.idle.pop_back();
			m_idle_lru.erase(idle_pos);

			if( reusable(idle.connection, idle.idle_since) )
				return std::move(idle.connection);

			if( idle.connection )
				ignore_unused(idle.connection->close());

			drop_count_locked(pos);
			pos = m_buckets.find(key);

			if( pos == m_buckets.end() )
				break;
		}
		return {};
	}

	bool evict_one_idle_locked() noexcept
	{
		if( m_idle_lru.empty() )
			return false;

		auto idle = m_idle_lru.begin();
		auto selected = m_buckets.find(idle->key);
		if( selected == m_buckets.end() or selected->second.idle.empty() )
			return false;

		assert(selected->second.idle.front() == idle);
		auto conn = std::move(idle->connection);
		selected->second.idle.pop_front();
		m_idle_lru.erase(idle);

		drop_count_locked(selected);
		if( conn )
			ignore_unused(conn->close());
		return true;
	}

	[[nodiscard]] sys_expected<lease_ptr> finish_connect
	(const target_t &key, size_t generation, connection_ptr conn, error_code error) noexcept
	{
		bool accepted = false;
		{
			std::lock_guard lock(m_mutex);
			auto pos = m_buckets.find(key);

			if( pos != m_buckets.end() and pos->second.connecting > 0 )
				--pos->second.connecting;

			if( m_stopped or generation != m_cancel_generation or error or
				not conn or not conn->is_open() )
			{
				drop_count_locked(pos);
				if( not error )
				{
					error = m_stopped or generation != m_cancel_generation ?
						make_error_code(errc::operation_aborted) :
						make_system_error_code(std::errc::not_connected);
				}
			}
			else
				accepted = true;
		}
		if( accepted )
			return lease_from_reserved(key, std::move(conn));

		if( conn )
			ignore_unused(conn->close());
		return sys_unexpected(error);
	}

	[[nodiscard]] sys_expected<lease_ptr> lease_from_reserved
	(const target_t &key, connection_ptr conn) noexcept
	{
		error_code error {};
		try {
			return make_lease(key, std::move(conn));
		}
		catch(const std::bad_alloc&) {
			error = make_system_error_code(std::errc::not_enough_memory);
		}
		catch(...) {
			error = make_system_error_code(std::errc::io_error);
		}
		std::lock_guard lock(m_mutex);
		drop_count_locked(m_buckets.find(key));
		return sys_unexpected(error);
	}

	[[nodiscard]] lease_ptr make_lease(const target_t &key, connection_ptr conn)
	{
		auto weak_self = this->weak_from_this();
		return std::make_shared<lease_t>(std::move(conn),
		[pool_weak = std::move(weak_self), key](connection_ptr released) mutable
		{
			if( auto self = pool_weak.lock() )
				self->give_back(key, std::move(released));
			else if( released )
				ignore_unused(released->close());
		});
	}

	void give_back(const target_t &key, connection_ptr conn) noexcept
	{
		const bool keep = reusable(conn);
		{
			std::lock_guard lock(m_mutex);
			auto pos = m_buckets.find(key);

			if( not m_stopped and keep and pos != m_buckets.end() )
			{
				try {
					m_idle_lru.push_back({std::move(conn), clock_t::now(), key});
					auto idle = std::prev(m_idle_lru.end());
					try {
						pos->second.idle.push_back(idle);
					}
					catch(...)
					{
						conn = std::move(idle->connection);
						m_idle_lru.erase(idle);
						throw;
					}
					if( not wake_matching_locked(key) )
						wake_any_locked();

					state_changed_locked();
					return ;
				}
				catch(...) {}
			}
			drop_count_locked(pos);
		}
		if( conn )
			ignore_unused(conn->close());
	}

	using bucket_iterator = std::unordered_map <
		target_t, connection_bucket, key_hash
	>::iterator;

	void drop_count_locked(bucket_iterator pos) noexcept
	{
		if( pos != m_buckets.end() )
		{
			if( pos->second.total > 0 )
				--pos->second.total;

			if( pos->second.total == 0 and
				pos->second.connecting == 0 and
				pos->second.idle.empty() )
				m_buckets.erase(pos);
		}
		if( m_total_count > 0 )
			--m_total_count;

		wake_any_locked();
		state_changed_locked();
	}

	void state_changed_locked() noexcept
	{
		++m_state_epoch;
		m_condition.notify_all();
	}

	void wake_waiter_locked
	(const std::shared_ptr<waiter> &item, wake_reason reason) noexcept
	{
		if( not item or item->reason != wake_reason::none )
			return ;
		item->reason = reason;
		try {
			ignore_unused(item->timer.cancel());
		}
		catch(...) {}
	}

	[[nodiscard]] bool enqueue_waiter_locked
	(const std::shared_ptr<waiter> &item) noexcept
	{
		bool all_inserted = false;
		try {
			m_waiters.emplace_back(item);
			item->all_position = std::prev(m_waiters.end());
			all_inserted = true;

			auto [key, inserted] = m_waiters_by_key.try_emplace(item->key);
			ignore_unused(inserted);
			try {
				key->second.emplace_back(item);
			}
			catch(...)
			{
				m_waiters.erase(item->all_position);
				all_inserted = false;

				if( key->second.empty() )
					m_waiters_by_key.erase(key);
				throw;
			}
			item->key_position = std::prev(key->second.end());
			item->queued = true;
			return true;
		}
		catch(...)
		{
			if( all_inserted )
				m_waiters.erase(item->all_position);
		}
		return false;
	}

	void dequeue_waiter_locked(waiter *item) noexcept
	{
		if( not item or not item->queued )
			return ;

		m_waiters.erase(item->all_position);
		if( auto key = m_waiters_by_key.find(item->key); key != m_waiters_by_key.end() )
		{
			key->second.erase(item->key_position);
			if( key->second.empty() )
				m_waiters_by_key.erase(key);
		}
		item->queued = false;
	}

	[[nodiscard]] bool wake_matching_locked(const target_t &key) noexcept
	{
		auto pos = m_waiters_by_key.find(key);
		if( pos == m_waiters_by_key.end() or pos->second.empty() )
			return false;

		auto item = pos->second.front();
		dequeue_waiter_locked(item.get());

		wake_waiter_locked(item, wake_reason::state_changed);
		return true;
	}

	void wake_any_locked() noexcept
	{
		if( m_waiters.empty() )
			return ;
		auto item = m_waiters.front();
		dequeue_waiter_locked(item.get());
		wake_waiter_locked(item, wake_reason::state_changed);
	}

	void remove_waiter_locked(waiter *value) noexcept
	{
		dequeue_waiter_locked(value);
	}

public:
	connector_ptr m_connector {};
	const config_t m_config {};
	executor_t m_exec {};

private:
	std::unordered_map <
		target_t, connection_bucket,key_hash
	> m_buckets {};

	idle_list m_idle_lru {};
	waiter_list m_waiters {};

	std::unordered_map <
		target_t, waiter_list, key_hash
	> m_waiters_by_key {};

	size_t m_total_count = 0;
	size_t m_state_epoch = 0;

	size_t m_cancel_generation = 0;
	bool m_stopped = false;

	mutable std::mutex m_mutex {};
	std::condition_variable m_condition {};
};

template <core_concepts::exec Exec>
basic_connection_pool<Exec>::basic_connection_pool(const config_t &config) requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	m_impl(std::make_shared<impl>(executor_t(riwo::get_executor()), config))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_connection_pool<Exec>::basic_connection_pool(Exec0 &&exec, const config_t &config) requires (
	not std::same_as<std::remove_cvref_t<Exec0>,basic_connection_pool> and
	core_concepts::match_sched<Exec0,executor_t> ):
	m_impl(std::make_shared<impl>(
		executor_t(get_executor_helper(std::forward<decltype(exec)>(exec))), config
	))
{

}

template <core_concepts::exec Exec>
basic_connection_pool<Exec>::basic_connection_pool
(connector_ptr connector_instance, const config_t &config) :
	m_impl(std::make_shared<impl>(std::move(connector_instance), config))
{

}

template <core_concepts::exec Exec>
basic_connection_pool<Exec>::~basic_connection_pool()
{
	if( m_impl )
		m_impl->invalidate();
}

template <core_concepts::exec Exec>
basic_connection_pool<Exec>::basic_connection_pool(basic_connection_pool &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

template <core_concepts::exec Exec>
basic_connection_pool<Exec>&
basic_connection_pool<Exec>::operator=(basic_connection_pool &&other) noexcept
{
	if( this == &other )
		return *this;

	if( m_impl )
		m_impl->invalidate();

	m_impl = std::move(other.m_impl);
	return *this;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_connection_pool<Exec>::get(const target_t &key, Token &&token) noexcept
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,lease_ptr>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = m_impl->acquire(key, true);
		token = result ? error_code{} : result.error();
		return result ? std::move(*result) : lease_ptr{};
	}
	else if constexpr( is_sync_opt_token_v<Token> )
		return m_impl->acquire(key, true);
	else
	{
		return initiate_expected<lease_ptr>(get_executor(),
		[impl = m_impl, key]() mutable -> awaitable<sys_expected<lease_ptr>> {
			co_return co_await impl->co_acquire(std::move(key), true);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_connection_pool<Exec>::try_get(const target_t &key, Token &&token) noexcept
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,lease_ptr>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = m_impl->acquire(key, false);
		token = result ? error_code{} : result.error();
		return result ? std::move(*result) : lease_ptr{};
	}
	else if constexpr( is_sync_opt_token_v<Token> )
		return m_impl->acquire(key, false);
	else
	{
		return initiate_expected<lease_ptr>(get_executor(),
		[impl = m_impl, key]() mutable -> awaitable<sys_expected<lease_ptr>> {
			co_return co_await impl->co_acquire(std::move(key), false);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
basic_connection_pool<Exec> &basic_connection_pool<Exec>::cancel() noexcept
{
	if( m_impl )
		m_impl->cancel();
	return *this;
}

template <core_concepts::exec Exec>
auto basic_connection_pool<Exec>::get_executor() noexcept -> executor_t
{
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
auto basic_connection_pool<Exec>::config() const noexcept -> config_t
{
	return m_impl->m_config;
}

template <core_concepts::exec Exec>
size_t basic_connection_pool<Exec>::count() const noexcept
{
	return m_impl ? m_impl->count() : 0;
}

} //namespace riwo::http


#endif //RIWO_HTTP_CLIENT_DETAIL_CONNECTION_POOL_H
