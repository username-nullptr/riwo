// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "local_interface.h"

#include <riwo/core/lock_free_queue.h>
#include <riwo/core/shared_mutex.h>
#include <riwo/core/jthread.h>

#include <riwo/utils/logger.h>

namespace riwo::utils::sbus { namespace detail
{

using payload_buffer_t = std::vector<std::byte>;
using shared_payload_t = std::shared_ptr<const payload_buffer_t>;

struct transparent_string_hash
{
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}

	[[nodiscard]] size_t operator()(const std::string &value) const noexcept {
		return operator()(std::string_view(value));
	}
};

class payload_t
{
public:
	payload_t(const void *data, size_t size) :
		m_shared(false)
	{
		if( size == 0 )
			new (&m_storage.owned) payload_buffer_t();
		else
		{
			auto begin = static_cast<const std::byte*>(data);
			new (&m_storage.owned) payload_buffer_t(begin, begin + size);
		}
	}

	explicit payload_t(shared_payload_t payload) noexcept :
		m_shared(true)
	{
		new (&m_storage.shared) shared_payload_t(std::move(payload));
	}

	payload_t(const payload_t &other) : m_shared(other.m_shared)
	{
		if( m_shared )
			new (&m_storage.shared) shared_payload_t(other.m_storage.shared);
		else
			new (&m_storage.owned) payload_buffer_t(other.m_storage.owned);
	}

	payload_t(payload_t &&other) noexcept : m_shared(other.m_shared)
	{
		if( m_shared )
			new (&m_storage.shared) shared_payload_t(std::move(other.m_storage.shared));
		else
			new (&m_storage.owned) payload_buffer_t(std::move(other.m_storage.owned));
	}

	~payload_t()
	{
		if( m_shared )
			m_storage.shared.~shared_payload_t();
		else
			m_storage.owned.~payload_buffer_t();
	}

	[[nodiscard]] const std::byte *data() const noexcept {
		return m_shared ? m_storage.shared->data() : m_storage.owned.data();
	}

	[[nodiscard]] size_t size() const noexcept {
		return m_shared ? m_storage.shared->size() : m_storage.owned.size();
	}

private:
	union storage_t
	{
		storage_t() noexcept {}
		~storage_t() {}

		payload_buffer_t owned;
		shared_payload_t shared;
	}
	m_storage;
	bool m_shared;
};

static_assert(sizeof(payload_t) <= sizeof(payload_buffer_t) + sizeof(void*));

constexpr size_t g_queue_max_size = 128;
constexpr size_t g_shared_payload_threshold = 64 * 1'024;

[[noreturn]] static void uncaught_exception(const std::exception &ex) noexcept
{
	riwo_utils_clog_critical("Riwo.Utils", "Uncaught exception: {}", ex);
	forced_termination();
}

namespace
{

#ifdef _MSC_VER
# pragma warning(push)
// The padding is intentional: these atomics occupy separate cache lines.
# pragma warning(disable: 4324)
#endif
class /* RIWO_DECL_HIDDEN */ subscriber_thread
{
	RIWO_DISABLE_COPY_MOVE(subscriber_thread)

protected:
	subscriber_thread() = default;

	void start(std::function<void()> task_arg)
	{
		m_thread = jthread([this, task = std::move(task_arg)]
		(const stop_token &token) mutable noexcept
		{
			try {
				do_task(task, token);
			}
			catch(const std::exception &ex) {
				uncaught_exception(ex);
			}
		});
	}

	void notify() noexcept
	{
		// A monotonic generation cannot be cleared over a concurrent enqueue.
		m_epoch.fetch_add(1, std::memory_order_release);
		std::atomic_notify_one(&m_epoch);
	}

	void stop() noexcept
	{
		if( not m_thread.joinable() )
			return ;

		m_thread.request_stop();
		notify();
		m_thread.join();
	}

public:
	virtual ~subscriber_thread() {
		stop();
	}

private:
	void do_task(const std::function<void()> &task, const riwo::stop_token &token)
	{
		uint64_t observed_epoch = 0;
		while( not token.stop_requested() )
		{
			while( m_epoch.load(std::memory_order_acquire) == observed_epoch )
			{
				std::atomic_wait_explicit (
					&m_epoch, observed_epoch, std::memory_order_acquire
				);
			}
			if( token.stop_requested() )
				break;
			do {
				observed_epoch = m_epoch.load(std::memory_order_acquire);
				task();
			}
			while (
				m_epoch.load(std::memory_order_acquire) != observed_epoch and
				not token.stop_requested()
			);
		}
	}

	alignas(64) std::atomic_uint64_t m_epoch {0};
	jthread m_thread {};
};
#ifdef _MSC_VER
# pragma warning(pop)
#endif

class /* RIWO_DECL_HIDDEN */ global_subscriber : public subscriber_thread
{
	RIWO_DISABLE_COPY_MOVE(global_subscriber)
	using callback_t = std::function<void(std::string_view,const payload_t&)>;

public:
	explicit global_subscriber(callback_t callback) :
		m_callback(std::move(callback))
	{
		start([this]
		{
			while( auto event = m_queue.dequeue() )
				m_callback(event->first, event->second);
		});
	}

	~global_subscriber() override {
		stop();
	}

	void tigger(std::string_view topic, const void *data, size_t size) noexcept
	{
		m_queue.force_emplace (
			std::make_pair(std::string(topic), payload_t(data, size))
		);
		notify();
	}

	void tigger(std::string_view topic, const shared_payload_t &payload) noexcept
	{
		m_queue.force_emplace (
			std::make_pair(std::string(topic), payload_t(payload))
		);
		notify();
	}

private:
	circular_lock_free_queue <
		std::pair<std::string,payload_t>, g_queue_max_size
	> m_queue {};

	callback_t m_callback {};
};

using global_subscriber_ptr = std::shared_ptr<global_subscriber>;

class /* RIWO_DECL_HIDDEN */ subscriber : public subscriber_thread
{
	RIWO_DISABLE_COPY_MOVE(subscriber)
	using callback_t = std::function<void(const payload_t&)>;

public:
	explicit subscriber(callback_t callback) :
		m_callback(std::move(callback))
	{
		start([this]
		{
			while( auto event = m_queue.dequeue() )
				m_callback(*event);
		});
	}

	~subscriber() override {
		stop();
	}

	void tigger(const void *data, size_t size) noexcept
	{
		m_queue.force_emplace(data, size);
		notify();
	}

	void tigger(const shared_payload_t &payload) noexcept
	{
		m_queue.force_emplace(payload);
		notify();
	}

private:
	circular_lock_free_queue<payload_t,g_queue_max_size> m_queue {};
	callback_t m_callback {};
};

using subscriber_ptr = std::shared_ptr<subscriber>;

}} //namespace detail

class RIWO_DECL_HIDDEN local_interface::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

	using subscriber_map = std::unordered_map <
		uint64_t, detail::subscriber_ptr
	>;
	using topic_map = std::unordered_map <
		std::string, subscriber_map, detail::transparent_string_hash, std::equal_to<>
	>;

public:
	impl() = default;

	[[nodiscard]] std::pair<uint64_t,detail::subscriber_ptr>
	make_subscriber(std::string_view topic, std::function<void(const detail::payload_t&)> callback) noexcept
	{
		auto id = m_id_seq++;
		auto obj = std::make_shared<detail::subscriber>(std::move(callback));
		std::unique_lock lock(m_subscribers_lock);

		auto it = m_subscribers.emplace (
			std::string(topic), std::unordered_map<uint64_t,detail::subscriber_ptr>()
		);
		it.first->second.emplace(id, obj);
		m_topics_by_sid.emplace(id, it.first->first);
		return { id, obj };
	}

	[[nodiscard]] std::pair<uint64_t,detail::global_subscriber_ptr>
	make_subscriber(std::function<void(std::string_view, const detail::payload_t&)> callback) noexcept
	{
		auto id = m_id_seq++;
		auto obj = std::make_shared<detail::global_subscriber>(std::move(callback));
		std::unique_lock lock(m_global_subscribers_lock);
		m_global_subscribers.emplace(id, obj);
		return { id, obj };
	}

	void global_broadcast(std::string_view topic, const void *data, size_t size) noexcept
	{
		std::shared_lock lock(m_global_subscribers_lock);
		if( m_global_subscribers.empty() )
			return ;

		for(auto &subscriber : m_global_subscribers | std::views::values)
			subscriber->tigger(topic, data, size);
	}

	void global_broadcast(std::string_view topic, const detail::shared_payload_t &payload) noexcept
	{
		std::shared_lock lock(m_global_subscribers_lock);
		for(auto &subscriber : m_global_subscribers | std::views::values)
			subscriber->tigger(topic, payload);
	}

	void broadcast(std::string_view topic, const void *data, size_t size) noexcept
	{
		std::shared_lock lock(m_subscribers_lock);
		auto it = m_subscribers.find(topic);
		if( it == m_subscribers.end() or it->second.empty() )
			return ;

		for(auto &subscriber : it->second | std::views::values)
			subscriber->tigger(data, size);
	}

	void broadcast(std::string_view topic, const detail::shared_payload_t &payload) noexcept
	{
		std::shared_lock lock(m_subscribers_lock);
		auto it = m_subscribers.find(topic);
		if( it == m_subscribers.end() )
			return ;

		for(auto &subscriber : it->second | std::views::values)
			subscriber->tigger(payload);
	}

	[[nodiscard]] size_t global_subscriber_count() const noexcept
	{
		std::shared_lock lock(m_global_subscribers_lock);
		return m_global_subscribers.size();
	}

	[[nodiscard]] size_t topic_subscriber_count(std::string_view topic) const noexcept
	{
		std::shared_lock lock(m_subscribers_lock);
		if( auto it = m_subscribers.find(topic); it != m_subscribers.end() )
			return it->second.size();
		return 0;
	}

public:
	std::atomic_uint64_t m_id_seq {0};
	topic_map m_subscribers {};

	std::unordered_map<uint64_t,std::string> m_topics_by_sid {};
	mutable shared_mutex m_subscribers_lock {};

	std::unordered_map<uint64_t,
		detail::global_subscriber_ptr
	> m_global_subscribers {};

	mutable shared_mutex m_global_subscribers_lock {};
};

using interface_set = std::unordered_set<local_interface*>;

using topic_interface_map = std::unordered_map <
	std::string, interface_set, detail::transparent_string_hash, std::equal_to<>
>;

static std::unordered_map <
	local_interface*, std::shared_ptr<local_interface>
> g_obj_map {};

static interface_set g_global_interfaces {};
static topic_interface_map g_topic_interfaces {};
static shared_mutex m_objs_lock {};

local_interface::local_interface() :
	m_impl(std::make_unique<impl>())
{

}

local_interface::~local_interface() = default;

void local_interface::publish(std::string_view topic, const void *buffer, size_t size)
{
	std::shared_lock lock(m_objs_lock);
	auto topic_pos = g_topic_interfaces.find(topic);

	if( g_global_interfaces.empty() and topic_pos == g_topic_interfaces.end() )
		return ;

	if( size >= detail::g_shared_payload_threshold )
	{
		size_t subscriber_count = 0;
		for(auto *obj : g_global_interfaces)
			subscriber_count += obj->m_impl->global_subscriber_count();

		if( topic_pos != g_topic_interfaces.end() )
		{
			for(auto *obj : topic_pos->second)
				subscriber_count += obj->m_impl->topic_subscriber_count(topic);
		}
		if( subscriber_count > 1 )
		{
			auto begin = static_cast<const std::byte*>(buffer);
			detail::shared_payload_t payload =
				std::make_shared<detail::payload_buffer_t>(begin, begin + size);

			for(auto *obj : g_global_interfaces)
				obj->m_impl->global_broadcast(topic, payload);

			if( topic_pos != g_topic_interfaces.end() )
			{
				for(auto *obj : topic_pos->second)
					obj->m_impl->broadcast(topic, payload);
			}
			return ;
		}
	}
	for(auto *obj : g_global_interfaces)
		obj->m_impl->global_broadcast(topic, buffer, size);

	if( topic_pos != g_topic_interfaces.end() )
	{
		for(auto *obj : topic_pos->second)
			obj->m_impl->broadcast(topic, buffer, size);
	}
}

uint64_t local_interface::subscribe(std::string_view topic, std::function<void(const void*, size_t)> callback)
{
	std::unique_lock objs_lock(m_objs_lock);
	auto [id, subr] = m_impl->make_subscriber(topic,
	[func = std::move(callback)](const detail::payload_t &payload) {
		func(payload.data(), payload.size());
	});
	ignore_unused(subr);
	g_obj_map.emplace(this, shared_from_this());
	g_topic_interfaces[std::string(topic)].emplace(this);
	return id;
}

uint64_t local_interface::subscribe(std::function<void(std::string_view topic, const void*, size_t)> callback)
{
	std::unique_lock objs_lock(m_objs_lock);
	auto [id, subr] = m_impl->make_subscriber(
	[func = std::move(callback)](std::string_view topic, const detail::payload_t &payload) {
		func(topic, payload.data(), payload.size());
	});
	ignore_unused(subr);
	g_obj_map.emplace(this, shared_from_this());
	g_global_interfaces.emplace(this);
	return id;
}

void local_interface::cancel_topic(std::string_view topic)
{
	std::unique_lock objs_lock(m_objs_lock);
	{
		std::unique_lock lock(m_impl->m_subscribers_lock);
		if( auto it = m_impl->m_subscribers.find(topic); it != m_impl->m_subscribers.end() )
		{
			for(auto &[sid, subscriber] : it->second)
			{
				ignore_unused(subscriber);
				m_impl->m_topics_by_sid.erase(sid);
			}
			m_impl->m_subscribers.erase(it);
		}
	}
	if( auto pos = g_topic_interfaces.find(topic); pos != g_topic_interfaces.end() )
	{
		pos->second.erase(this);
		if( pos->second.empty() )
			g_topic_interfaces.erase(pos);
	}
	std::shared_lock global_lock(m_impl->m_global_subscribers_lock);
	std::shared_lock topic_lock(m_impl->m_subscribers_lock);

	if( m_impl->m_global_subscribers.empty() and m_impl->m_subscribers.empty() )
		g_obj_map.erase(this);
}

void local_interface::cancel_sid(uint64_t sid)
{
	std::unique_lock objs_lock(m_objs_lock);
	bool erased = false;
	bool global_empty = false;
	{
		std::unique_lock lock(m_impl->m_global_subscribers_lock);
		erased = m_impl->m_global_subscribers.erase(sid) > 0;
		global_empty = m_impl->m_global_subscribers.empty();
	}
	std::string topic {};
	bool topic_subscription = false;
	bool topic_empty = false;

	if( not erased )
	{
		std::unique_lock lock(m_impl->m_subscribers_lock);
		auto sid_pos = m_impl->m_topics_by_sid.find(sid);

		if( sid_pos != m_impl->m_topics_by_sid.end() )
		{
			topic_subscription = true;
			topic = sid_pos->second;

			m_impl->m_topics_by_sid.erase(sid_pos);
			if( auto pos = m_impl->m_subscribers.find(topic); pos != m_impl->m_subscribers.end() )
			{
				erased = pos->second.erase(sid) > 0;
				topic_empty = pos->second.empty();

				if( topic_empty )
					m_impl->m_subscribers.erase(pos);
			}
		}
	}
	if( not erased )
		return ;

	if( not topic_subscription )
	{
		if( global_empty )
			g_global_interfaces.erase(this);
	}
	else if( topic_empty )
	{
		if( auto pos = g_topic_interfaces.find(topic); pos != g_topic_interfaces.end() )
		{
			pos->second.erase(this);
			if( pos->second.empty() )
				g_topic_interfaces.erase(pos);
		}
	}
	std::shared_lock global_lock(m_impl->m_global_subscribers_lock);
	std::shared_lock topic_lock(m_impl->m_subscribers_lock);

	if( m_impl->m_global_subscribers.empty() and m_impl->m_subscribers.empty() )
		g_obj_map.erase(this);
}

void local_interface::cancel()
{
	std::unique_lock objs_lock(m_objs_lock);
	for(auto &[topic, subscribers] : m_impl->m_subscribers)
	{
		ignore_unused(subscribers);
		if( auto pos = g_topic_interfaces.find(topic); pos != g_topic_interfaces.end() )
		{
			pos->second.erase(this);
			if( pos->second.empty() )
				g_topic_interfaces.erase(pos);
		}
	}
	g_global_interfaces.erase(this);

	m_impl->m_global_subscribers_lock.lock();
	m_impl->m_global_subscribers.clear();
	m_impl->m_global_subscribers_lock.unlock();

	m_impl->m_subscribers_lock.lock();
	m_impl->m_subscribers.clear();
	m_impl->m_topics_by_sid.clear();
	m_impl->m_subscribers_lock.unlock();

	g_obj_map.erase(this);
}

} //namespace riwo::utils::sbus
