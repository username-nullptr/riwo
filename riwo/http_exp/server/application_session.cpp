// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "application_session.h"
#include <random>

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] std::string make_session_id()
{
	static std::atomic<uint64_t> sequence {0};
	std::array<unsigned char,24> bytes {};
	std::random_device random;
	for( auto &value : bytes ) value = static_cast<unsigned char>(random());
	auto counter = sequence.fetch_add(1, std::memory_order_relaxed);
	for( size_t index = 0; index < sizeof(counter); ++index )
		bytes[index] ^= static_cast<unsigned char>(counter >> (index * 8U));
	constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string result;
	result.reserve(32);
	uint32_t accumulator = 0;
	unsigned bits = 0;
	for( const auto value : bytes )
	{
		accumulator = (accumulator << 8U) | value;
		bits += 8;
		while( bits >= 6 )
		{
			bits -= 6;
			result.push_back(alphabet[(accumulator >> bits) & 0x3fU]);
		}
	}
	return result;
}

} //namespace

application_session::application_session(std::chrono::seconds lifetime) :
	m_id(make_session_id()), m_created(clock_t::now()),
	m_expires(m_created + std::max(lifetime, std::chrono::seconds::zero())),
	m_lifetime(std::max(lifetime, std::chrono::seconds::zero()))
{
}

application_session::~application_session() = default;

std::string_view application_session::id() const noexcept { return m_id; }
auto application_session::create_time() const noexcept -> time_point_t {
	return m_created;
}

auto application_session::expires_at() const noexcept -> time_point_t {
	std::lock_guard lock(m_mutex);
	return m_expires;
}

std::chrono::seconds application_session::lifecycle() const noexcept {
	std::lock_guard lock(m_mutex);
	return m_lifetime;
}

bool application_session::valid() const noexcept {
	std::lock_guard lock(m_mutex);
	return m_valid and clock_t::now() < m_expires;
}

optional<std::any> application_session::attribute(std::string_view key) const
{
	std::lock_guard lock(m_mutex);
	const auto found = m_attributes.find(std::string(key));
	if( found == m_attributes.end() )
		return {};
	return found->second;
}

application_session &application_session::set_attribute(
	std::string key, std::any value)
{
	std::lock_guard lock(m_mutex);
	m_attributes.insert_or_assign(std::move(key), std::move(value));
	return *this;
}

application_session &application_session::erase_attribute(
	std::string_view key) noexcept
{
	try
	{
		std::lock_guard lock(m_mutex);
		m_attributes.erase(std::string(key));
	}
	catch(...) {
	}
	return *this;
}

void application_session::clear_attributes() noexcept {
	std::lock_guard lock(m_mutex);
	m_attributes.clear();
}

application_session &application_session::set_lifecycle(
	std::chrono::seconds value)
{
	std::lock_guard lock(m_mutex);
	m_lifetime = std::max(value, std::chrono::seconds::zero());
	m_expires = clock_t::now() + m_lifetime;
	return *this;
}

application_session &application_session::refresh()
{
	std::lock_guard lock(m_mutex);
	if( m_valid )
		m_expires = clock_t::now() + m_lifetime;
	return *this;
}

application_session &application_session::expand(std::chrono::seconds value)
{
	std::lock_guard lock(m_mutex);
	if( m_valid )
		m_expires += std::max(value, std::chrono::seconds::zero());
	return *this;
}

void application_session::invalidate() noexcept {
	std::lock_guard lock(m_mutex);
	m_valid = false;
}

application_session &application_session::on_timeout(timeout_handler_t handler)
{
	std::lock_guard lock(m_mutex);
	m_timeout_handler = std::move(handler);
	return *this;
}

application_session &application_session::unbind_timeout() noexcept
{
	std::lock_guard lock(m_mutex);
	m_timeout_handler = {};
	return *this;
}

void application_session::notify_timeout() noexcept
{
	timeout_handler_t handler;
	{
		std::lock_guard lock(m_mutex);
		if( m_timeout_notified )
			return ;
		m_timeout_notified = true;
		m_valid = false;
		handler = m_timeout_handler;
	}
	try { if( handler )
		handler(*this); }
	catch(...) {
	}
}

class application_session_manager::impl
{
public:
	explicit impl(application_session_config value) : config(std::move(value)) {}
	mutable std::mutex mutex;
	application_session_config config;
	std::unordered_map<std::string,application_session_ptr> sessions;
};

application_session_manager::application_session_manager(
	application_session_config config) :
	m_impl(std::make_unique<impl>(std::move(config)))
{
}

application_session_manager::~application_session_manager() = default;
application_session_manager::application_session_manager(
	application_session_manager &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(application_session_config{});
}
application_session_manager &application_session_manager::operator=(
	application_session_manager &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(application_session_config{});
	return *this;
}

application_session_config application_session_manager::config() const
{
	std::lock_guard lock(m_impl->mutex);
	return m_impl->config;
}

void application_session_manager::set_config(application_session_config config)
{
	std::lock_guard lock(m_impl->mutex);
	m_impl->config = std::move(config);
}

void application_session_manager::insert(application_session_ptr value)
{
	if( not value )
		throw std::invalid_argument(
		"riwo::http_exp::application_session_manager: null session");
	std::lock_guard lock(m_impl->mutex);
	m_impl->sessions.insert_or_assign(std::string(value->id()), std::move(value));
}

application_session_ptr application_session_manager::create()
{
	application_session_config current;
	{
		std::lock_guard lock(m_impl->mutex);
		current = m_impl->config;
	}
	auto result = std::make_shared<application_session>(current.lifetime);
	insert(result);
	return result;
}

application_session_ptr application_session_manager::get(std::string_view id)
{
	if( id.empty() )
		return {};
	application_session_ptr expired;
	{
		std::lock_guard lock(m_impl->mutex);
		const auto found = m_impl->sessions.find(std::string(id));
		if( found == m_impl->sessions.end() )
			return {};
		if( found->second->valid() )
			return found->second;
		expired = found->second;
		m_impl->sessions.erase(found);
	}
	expired->notify_timeout();
	return {};
}

application_session_ptr application_session_manager::get_or_create(
	std::string_view id)
{
	if( auto current = get(id) )
	{
		current->refresh();
		return current;
	}
	return create();
}

bool application_session_manager::erase(std::string_view id) noexcept
{
	try
	{
		std::lock_guard lock(m_impl->mutex);
		return m_impl->sessions.erase(std::string(id)) != 0;
	}
	catch(...) {
		return false;
	}
}

size_t application_session_manager::purge_expired() noexcept
{
	std::vector<application_session_ptr> expired;
	{
		std::lock_guard lock(m_impl->mutex);
		for( auto iterator = m_impl->sessions.begin();
			iterator != m_impl->sessions.end(); )
		{
			if( iterator->second->valid() )
				++iterator;
			else
			{
				expired.emplace_back(iterator->second);
				iterator = m_impl->sessions.erase(iterator);
			}
		}
	}
	for( const auto &value : expired ) value->notify_timeout();
	return expired.size();
}

size_t application_session_manager::size() const noexcept
{
	std::lock_guard lock(m_impl->mutex);
	return m_impl->sessions.size();
}

} //namespace riwo::http_exp
