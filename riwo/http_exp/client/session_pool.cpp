// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/session_pool.h"
#include <condition_variable>
#include <deque>
#include <mutex>

namespace riwo::http_exp::detail { namespace
{

using clock_type = std::chrono::steady_clock;
using timer_type = asio::steady_timer;

class pooled_stream final : public stream,
	public std::enable_shared_from_this<pooled_stream>
{
	RIWO_DISABLE_COPY_MOVE(pooled_stream)

public:
	using abandon_handler = std::function<void(bool)>;

	pooled_stream(std::shared_ptr<stream> inner, abandon_handler on_abandon) :
		m_inner(std::move(inner)), m_on_abandon(std::move(on_abandon))
	{
	}

	~pooled_stream() override {
		if( m_inner and not m_inner->reusable() ) m_inner->cancel();
		abandon(false);
	}

	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return m_inner->selected_protocol();
	}
	[[nodiscard]] protocol response_protocol() const noexcept override {
		return m_inner->response_protocol();
	}
	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return m_inner->capabilities();
	}
	[[nodiscard]] request_head request() const override {
		return m_inner->request();
	}
	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override {
		return m_inner->inbound_body();
	}
	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override {
		return m_inner->outbound_body();
	}
	[[nodiscard]] request_delivery delivery() const noexcept override {
		return m_inner->delivery();
	}
	[[nodiscard]] response_head wait_response_sync(
		error_code &error) noexcept override {
		return m_inner->wait_response_sync(error);
	}
	void wait_response(response_handler handler) override {
		m_inner->wait_response(std::move(handler));
	}
	void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept override {
		m_inner->respond_sync(std::move(head), std::move(spec), error);
	}
	void respond(response_head head, body_spec spec,
		operation_handler handler) override {
		m_inner->respond(std::move(head), std::move(spec), std::move(handler));
	}
	void inform_sync(response_head head, error_code &error) noexcept override {
		m_inner->inform_sync(std::move(head), error);
	}
	void inform(response_head head, operation_handler handler) override {
		m_inner->inform(std::move(head), std::move(handler));
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return m_inner->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return m_inner->local_endpoint(error);
	}
	[[nodiscard]] std::shared_ptr<duplex_state> open_duplex_sync(
		optional<response_head> response, error_code &error) noexcept override
	{
		auto result = m_inner->open_duplex_sync(std::move(response), error);
		if( not error ) abandon(false);
		return result;
	}
	void open_duplex(optional<response_head> response,
		duplex_handler handler) override
	{
		auto self = shared_from_this();
		m_inner->open_duplex(std::move(response),
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, std::shared_ptr<duplex_state> state) mutable {
			if( not error ) self->abandon(false);
			std::move(handler)(error, std::move(state));
		});
	}
	void cancel() noexcept override {
		m_inner->cancel();
		abandon(false);
	}

private:
	void abandon(bool reusable) noexcept
	{
		if( m_abandoned ) return;
		m_abandoned = true;
		try {
			if( m_on_abandon ) m_on_abandon(reusable);
		}
		catch(...) {
		}
	}

	std::shared_ptr<stream> m_inner;
	abandon_handler m_on_abandon;
	bool m_abandoned = false;
};

} //namespace

class session_pool::impl : public std::enable_shared_from_this<session_pool::impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	struct waiter;
	using waiter_ptr = std::shared_ptr<waiter>;

	struct slot
	{
		std::shared_ptr<client_session> session {};
		bool busy = false;
		uint64_t generation = 0;
		clock_type::time_point idle_since {};
		std::shared_ptr<timer_type> idle_timer {};
		bool removed = false;
	};

	using slot_ptr = std::shared_ptr<slot>;

	struct waiter
	{
		waiter(session_key session_key_value, request_head head_value,
			body_spec spec_value, open_handler completion) :
			key(std::move(session_key_value)), head(std::move(head_value)),
			spec(std::move(spec_value)), handler(std::move(completion))
		{
		}

		session_key key;
		request_head head;
		body_spec spec;
		open_handler handler;
		std::shared_ptr<timer_type> timer;
		bool completed = false;
		bool queued = false;
	};

	struct bucket
	{
		std::vector<slot_ptr> sessions {};
		std::deque<waiter_ptr> waiters {};
		size_t connecting = 0;
		bool pump_scheduled = false;
	};

public:
	impl(executor_type executor_value, client_config config_value,
		connector_ptr connector_value, session_factory_ptr factory_value) :
		m_executor(std::move(executor_value)), m_config(std::move(config_value)),
		m_connector(std::move(connector_value)), m_factory(std::move(factory_value))
	{
		if( not m_connector ) m_connector = make_default_connector(m_executor);
		if( not m_factory ) m_factory = make_default_session_factory();
	}

	~impl() {
		shutdown();
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(const session_key &key,
		request_head head, body_spec spec, error_code &error) noexcept
	{
		error.clear();
		try {
			for(;;)
			{
				slot_ptr selected;
				{
					std::unique_lock lock(m_mutex);
					if( m_stopped )
					{
						error = asio::error::make_error_code(
							asio::error::operation_aborted);
						return {};
					}
					if( m_config.pool.max_sessions_per_origin == 0 )
					{
						error = make_error_code(errc::operation_not_permitted);
						return {};
					}
					auto &value = m_buckets[key];
					selected = take_slot_locked(value);
					if( not selected )
					{
						if( value.sessions.size() + value.connecting <
							m_config.pool.max_sessions_per_origin )
						{
							++value.connecting;
						}
						else
						{
							if( not m_condition.wait_for(lock,
								std::chrono::milliseconds(m_config.timeout.pool_wait),
								[this, &key] {
									auto iterator = m_buckets.find(key);
									return m_stopped or iterator == m_buckets.end() or
										has_capacity_locked(iterator->second) or
										iterator->second.sessions.size() +
											iterator->second.connecting <
											m_config.pool.max_sessions_per_origin;
								}) )
							{
								error = asio::error::make_error_code(
									asio::error::timed_out);
								return {};
							}
							continue;
						}
					}
				}

				if( selected )
				{
					auto active = selected->session->open_sync(std::move(head),
						std::move(spec), error);
					if( error )
					{
						release_slot(key, selected, false);
						return {};
					}
				#if RIWO_HTTP_EXP_HTTP2_SUPPORT
					if( selected->session->selected_protocol() == protocol::v2 )
						release_slot(key, selected, true);
				#endif
					return wrap(key, selected, std::move(active));
				}

				auto transport = m_connector->connect(key, m_config);
				std::shared_ptr<client_session> session;
				if( transport )
				{
					auto created = m_factory->create(key, std::move(*transport),
						m_config);
					if( created )
						session = std::move(*created);
					else
						error = created.error();
				}
				else
					error = transport.error();

				{
					std::lock_guard lock(m_mutex);
					auto &value = m_buckets[key];
					if( value.connecting > 0 ) --value.connecting;
				}
				if( error or not session )
				{
					if( not error )
						error = make_error_code(errc::negotiation_failed);
					return {};
				}
				add_session(key, std::move(session));
			}
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return {};
		}
	}

	void open(const session_key &key, request_head head, body_spec spec,
		open_handler handler)
	{
		try {
			auto item = std::make_shared<waiter>(key, std::move(head),
				std::move(spec), std::move(handler));
			error_code immediate;
			{
				std::lock_guard lock(m_mutex);
				if( m_stopped )
					immediate = asio::error::make_error_code(
						asio::error::operation_aborted);
				else
				{
					auto &value = m_buckets[key];
					if( value.waiters.size() >=
						m_config.pool.max_waiters_per_origin )
						immediate = make_error_code(
							errc::buffer_limit_exceeded);
					else
					{
						item->queued = true;
						value.waiters.push_back(item);
						schedule_idle(item);
					}
				}
			}
			if( immediate )
			{
				complete(item, immediate, {});
				return;
			}
			schedule_pump(key);
		}
		catch(...) {
			std::move(handler)(exception_error(std::current_exception()),
				std::shared_ptr<stream>{});
		}
	}

	void cancel() noexcept
	{
		std::vector<waiter_ptr> failed;
		std::vector<std::shared_ptr<client_session>> sessions;
		{
			std::lock_guard lock(m_mutex);
			m_stopped = true;
			for( auto &[key, value] : m_buckets )
			{
				ignore_unused(key);
				for( auto &item : value.waiters )
				{
					item->queued = false;
					failed.push_back(item);
				}
				value.waiters.clear();
				for( auto &value_slot : value.sessions )
				{
					if( value_slot->session )
						sessions.push_back(value_slot->session);
				}
			}
			m_condition.notify_all();
		}
		for( auto &item : failed )
			complete(item, asio::error::make_error_code(
				asio::error::operation_aborted), {});
		for( auto &session : sessions )
			session->graceful_shutdown(
				[](error_code) {});
	}

	void shutdown() noexcept
	{
		cancel();
	}

	[[nodiscard]] size_t session_count() const noexcept
	{
		std::lock_guard lock(m_mutex);
		size_t result = 0;
		for( const auto &[key, value] : m_buckets )
		{
			ignore_unused(key);
			result += value.sessions.size();
		}
		return result;
	}

	[[nodiscard]] size_t idle_session_count() const noexcept
	{
		std::lock_guard lock(m_mutex);
		size_t result = 0;
		for( const auto &[key, value] : m_buckets )
		{
			ignore_unused(key);
			for( const auto &item : value.sessions )
				if( not item->busy and not item->session->closed() and
					not item->session->draining() )
					++result;
		}
		return result;
	}

	[[nodiscard]] size_t waiter_count() const noexcept
	{
		std::lock_guard lock(m_mutex);
		size_t result = 0;
		for( const auto &[key, value] : m_buckets )
		{
			ignore_unused(key);
			result += value.waiters.size();
		}
		return result;
	}

	executor_type m_executor;
	client_config m_config;
	connector_ptr m_connector;
	session_factory_ptr m_factory;

private:
	[[nodiscard]] static bool usable(const slot_ptr &value)
	{
		return value and value->session and not value->busy and
			not value->removed and not value->session->closed() and
			not value->session->draining() and
			value->session->available_stream_capacity() != 0;
	}

	[[nodiscard]] static bool has_capacity_locked(const bucket &value)
	{
		return std::ranges::any_of(value.sessions,
			[](const slot_ptr &item) { return usable(item); });
	}

	[[nodiscard]] slot_ptr take_slot_locked(bucket &value)
	{
		for( auto &item : value.sessions )
		{
			if( not usable(item) ) continue;
		#if RIWO_HTTP_EXP_HTTP2_SUPPORT
			if( item->session->selected_protocol() == protocol::v2 and
				not item->session->try_reserve_stream() ) continue;
		#endif
			item->busy = true;
			++item->generation;
			if( item->idle_timer )
			{
				ignore_unused(item->idle_timer->cancel());
			}
			return item;
		}
		return {};
	}

	void add_session(const session_key &key,
		std::shared_ptr<client_session> session)
	{
		auto holder = std::make_shared<slot>();
		holder->session = std::move(session);
		auto weak = weak_from_this();
		holder->session->set_release_handler(
		[weak, key, slot_value = std::weak_ptr<slot>(holder)]
		(bool reusable) {
			if( auto self = weak.lock() )
				if( auto active = slot_value.lock() )
					self->release_slot(key, active, reusable);
		});

		{
			std::lock_guard lock(m_mutex);
			auto &value = m_buckets[key];
			value.sessions.push_back(holder);
			m_condition.notify_all();
		}
		schedule_pump(key);
	}

	void schedule_pump(const session_key &key)
	{
		{
			std::lock_guard lock(m_mutex);
			auto iterator = m_buckets.find(key);
			if( iterator == m_buckets.end() or iterator->second.pump_scheduled )
				return;
			iterator->second.pump_scheduled = true;
		}
		auto weak = weak_from_this();
		asio::post(m_executor,
		[weak, key] {
			if( auto self = weak.lock() ) self->pump(key);
		});
	}

	void pump(const session_key &key)
	{
		for(;;)
		{
			waiter_ptr item;
			waiter_ptr rejected;
			slot_ptr selected;
			bool connect = false;
			{
				std::lock_guard lock(m_mutex);
				auto iterator = m_buckets.find(key);
				if( iterator == m_buckets.end() )
					return;
				auto &value = iterator->second;
				value.pump_scheduled = false;
				if( m_stopped or value.waiters.empty() )
					return;
				if( m_config.pool.max_sessions_per_origin == 0 )
				{
					item = value.waiters.front();
					value.waiters.pop_front();
					item->queued = false;
					rejected = item;
					item.reset();
				}
				else
				{
					selected = take_slot_locked(value);
					if( selected )
					{
						item = value.waiters.front();
						value.waiters.pop_front();
						item->queued = false;
					}
					else if( value.sessions.size() + value.connecting <
						m_config.pool.max_sessions_per_origin )
					{
						++value.connecting;
						connect = true;
					}
					else
						return;
				}
			}

			if( rejected )
			{
				complete(rejected,
					make_error_code(errc::operation_not_permitted), {});
				schedule_pump(key);
				return;
			}

			if( connect )
			{
				start_connect(key);
				return;
			}
			if( not item or not selected ) return;

			auto session = selected->session;
			auto weak = weak_from_this();
			session->open(item->head, item->spec,
			[weak, key, selected, item]
			(error_code error, std::shared_ptr<stream> active) mutable {
				if( auto self = weak.lock() )
				{
					if( error )
					{
						self->release_slot(key, selected, false);
						self->complete(item, error, {});
						self->schedule_pump(key);
						return;
					}
				#if RIWO_HTTP_EXP_HTTP2_SUPPORT
					if( selected->session->selected_protocol() == protocol::v2 )
						self->release_slot(key, selected, true);
				#endif
					self->complete(item, error_code{},
						self->wrap(key, selected, std::move(active)));
					self->schedule_pump(key);
				}
				else
					self_finish_unused(item, error);
			});
			return;
		}
	}

	void start_connect(const session_key &key)
	{
		auto weak = weak_from_this();
		m_connector->connect_async(key, m_config,
		[weak, key](error_code error, negotiated_transport transport) mutable {
			auto self = weak.lock();
			if( not self ) return;
			if( error )
				return self->connect_failed(key, error);

			sys_expected<std::shared_ptr<client_session>> created =
				sys_unexpected(make_error_code(errc::negotiation_failed));
			try {
				created = self->m_factory->create(key, std::move(transport),
					self->m_config);
			}
			catch(...) {
				created = sys_unexpected(
					exception_error(std::current_exception()));
			}
			if( not created )
				return self->connect_failed(key, created.error());
			{
				std::lock_guard lock(self->m_mutex);
				auto iterator = self->m_buckets.find(key);
				if( iterator != self->m_buckets.end() and
					iterator->second.connecting > 0 )
					--iterator->second.connecting;
			}
			self->add_session(key, std::move(*created));
		});
	}

	void connect_failed(const session_key &key, error_code error)
	{
		waiter_ptr item;
		{
			std::lock_guard lock(m_mutex);
			auto iterator = m_buckets.find(key);
			if( iterator == m_buckets.end() ) return;
			auto &value = iterator->second;
			if( value.connecting > 0 ) --value.connecting;
			if( not value.waiters.empty() )
			{
				item = value.waiters.front();
				value.waiters.pop_front();
				item->queued = false;
			}
			m_condition.notify_all();
		}
		if( item ) complete(item, error, {});
		schedule_pump(key);
	}

	void release_slot(const session_key &key, const slot_ptr &value,
		bool reusable)
	{
		std::shared_ptr<client_session> session;
		bool remove = false;
		{
			std::lock_guard lock(m_mutex);
			if( not value or value->removed ) return;
			const bool was_busy = value->busy;
			ignore_unused(was_busy);
			if( value->busy ) value->busy = false;
			value->idle_since = clock_type::now();
			session = value->session;
			if( not reusable or not session or session->closed() or
				session->draining() )
				remove = value->removed = true;
			else
			{
			#if RIWO_HTTP_EXP_HTTP2_SUPPORT
				if( session->selected_protocol() != protocol::v2 or not was_busy )
			#endif
				schedule_idle_locked(key, value);
			}
			m_condition.notify_all();
		}
		if( remove and session )
			session->graceful_shutdown([](error_code) {});
		if( remove ) remove_slot(key, value);
		schedule_pump(key);
	}

	void remove_slot(const session_key &key, const slot_ptr &value)
	{
		{
			std::lock_guard lock(m_mutex);
			auto iterator = m_buckets.find(key);
			if( iterator == m_buckets.end() ) return;
			auto &values = iterator->second.sessions;
			std::erase_if(values, [&value](const slot_ptr &item) {
				return item == value;
			});
		}
		schedule_pump(key);
	}

	void schedule_idle(const waiter_ptr &value)
	{
		if( m_config.timeout.pool_wait.count() <= 0 ) return;
		auto weak = weak_from_this();
		value->timer = std::make_shared<timer_type>(m_executor);
		value->timer->expires_after(m_config.timeout.pool_wait);
		value->timer->async_wait(
		[weak, value](error_code error) {
			if( error or value->completed ) return;
			if( auto self = weak.lock() ) self->wait_timed_out(value);
		});
	}

	void schedule_idle_locked(const session_key &key, const slot_ptr &value)
	{
		if( m_config.pool.idle_timeout.count() <= 0 ) return;
		value->idle_timer = std::make_shared<timer_type>(m_executor);
		value->idle_timer->expires_after(m_config.pool.idle_timeout);
		auto weak = weak_from_this();
		auto timer = value->idle_timer;
		timer->async_wait(
		[weak, key, value](error_code error) {
			if( error ) return;
			if( auto self = weak.lock() ) self->expire_idle(key, value);
		});
	}

	void wait_timed_out(const waiter_ptr &value)
	{
		{
			std::lock_guard lock(m_mutex);
			if( value->completed or not value->queued ) return;
			auto iterator = m_buckets.find(value->key);
			if( iterator == m_buckets.end() ) return;
			auto &items = iterator->second.waiters;
			std::erase(items, value);
			value->queued = false;
		}
		complete(value, asio::error::make_error_code(asio::error::timed_out),
			{});
		schedule_pump(value->key);
	}

	void expire_idle(const session_key &key, const slot_ptr &value)
	{
		std::shared_ptr<client_session> session;
		{
			std::lock_guard lock(m_mutex);
			if( not value or value->busy or value->removed ) return;
			value->removed = true;
			session = value->session;
		}
		if( session ) session->graceful_shutdown([](error_code) {});
		remove_slot(key, value);
	}

	[[nodiscard]] std::shared_ptr<stream> wrap(const session_key &key,
		const slot_ptr &value, std::shared_ptr<stream> active)
	{
		if( not active )
		{
			release_slot(key, value, false);
			return {};
		}
		auto weak = weak_from_this();
		const auto generation = value->generation;
		return std::make_shared<pooled_stream>(std::move(active),
		[weak, key, value, generation](bool reusable) {
			if( auto self = weak.lock() )
				self->abandon(key, value, generation, reusable);
		});
	}

	void abandon(const session_key &key, const slot_ptr &value,
		uint64_t generation, bool reusable)
	{
		std::shared_ptr<client_session> session;
		{
			std::lock_guard lock(m_mutex);
			if( not value or not value->busy or value->generation != generation )
				return;
			session = value->session;
		}
		if( session ) session->release_stream(reusable);
		release_slot(key, value, reusable);
	}

	void complete(const waiter_ptr &value, error_code error,
		std::shared_ptr<stream> active)
	{
		if( not value or value->completed ) return;
		value->completed = true;
		if( value->timer )
		{
			ignore_unused(value->timer->cancel());
		}
		try {
			std::move(value->handler)(error, std::move(active));
		}
		catch(...) {
		}
	}

	static void self_finish_unused(const waiter_ptr &item, error_code error)
	{
		if( item and not item->completed )
		{
			item->completed = true;
			std::move(item->handler)(error, std::shared_ptr<stream>{});
		}
	}

	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	std::unordered_map<session_key, bucket, session_key_hash> m_buckets;
	bool m_stopped = false;
};

session_pool::session_pool(executor_type executor, client_config config,
	connector_ptr connector, session_factory_ptr factory) :
	m_impl(std::make_shared<impl>(std::move(executor), std::move(config),
		std::move(connector), std::move(factory)))
{
}

session_pool::~session_pool() = default;

auto session_pool::get_executor() const noexcept -> executor_type {
	return m_impl->m_executor;
}

auto session_pool::config() const noexcept -> const client_config & {
	return m_impl->m_config;
}

std::shared_ptr<stream> session_pool::open_sync(const session_key &key,
	request_head head, body_spec spec, error_code &error) noexcept
{
	return m_impl->open_sync(key, std::move(head), std::move(spec), error);
}

std::shared_ptr<stream> session_pool::open_sync(const origin &target,
	request_head head, body_spec spec, error_code &error) noexcept
{
	auto key = make_session_key(head, m_impl->m_config);
	if( not target.scheme.empty() ) key.scheme = target.scheme;
	if( not target.host.empty() ) key.host = target.host;
	if( target.port != 0 ) key.port = target.port;
	key.tls.enabled = key.scheme == "https";
	return m_impl->open_sync(key, std::move(head), std::move(spec), error);
}

void session_pool::open(const session_key &key, request_head head,
	body_spec spec, open_handler handler)
{
	m_impl->open(key, std::move(head), std::move(spec), std::move(handler));
}

void session_pool::open(const origin &target, request_head head,
	body_spec spec, open_handler handler)
{
	auto key = make_session_key(head, m_impl->m_config);
	if( not target.scheme.empty() ) key.scheme = target.scheme;
	if( not target.host.empty() ) key.host = target.host;
	if( target.port != 0 ) key.port = target.port;
	key.tls.enabled = key.scheme == "https";
	m_impl->open(key, std::move(head), std::move(spec), std::move(handler));
}

void session_pool::cancel() noexcept {
	m_impl->cancel();
}

void session_pool::shutdown() noexcept {
	m_impl->shutdown();
}

size_t session_pool::session_count() const noexcept {
	return m_impl->session_count();
}

size_t session_pool::idle_session_count() const noexcept {
	return m_impl->idle_session_count();
}

size_t session_pool::waiter_count() const noexcept {
	return m_impl->waiter_count();
}

} //namespace riwo::http_exp::detail
