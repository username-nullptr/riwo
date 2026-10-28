// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "app_session.h"
#include <riwo/core/algorithm/base64.h>
#include <riwo/core/algorithm/secure_random.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] std::string make_session_id()
{
	std::array<std::byte,32> bytes {};
	auto random = secure_random_bytes(mutable_buffer(bytes.data(), bytes.size()));
	sys_expected_loc_throw(random, "Failed to generate HTTP application session ID");

	auto result = base64_encode(bytes);
	for(auto &value : result)
	{
		if( value == '+' ) value = '-';
		else if( value == '/' ) value = '_';
	}
	while( not result.empty() and result.back() == '=' )
		result.pop_back();
	return result;
}

} //namespace

class RIWO_DECL_HIDDEN app_session::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(std::chrono::seconds value) :
		m_id(make_session_id()), m_created(clock_t::now()),
		m_expires(m_created + std::max(value, std::chrono::seconds::zero())),
		m_lifetime(std::max(value, std::chrono::seconds::zero())) {}

public:
	std::string m_id {};
	time_point_t m_created {};

	time_point_t m_expires {};
	std::chrono::seconds m_lifetime {};

	mutable std::mutex m_mutex {};

	std::unordered_map<std::string,std::any> m_attributes {};
	timeout_handler_t m_timeout_handler {};

	bool m_valid = true;
	bool m_timeout_notified = false;
};

app_session::app_session(std::chrono::seconds lifetime) :
	m_impl(std::make_unique<impl>(lifetime))
{

}

app_session::~app_session() = default;

void app_session::clear_attributes() noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_attributes.clear();
}

app_session &app_session::set_lifecycle(std::chrono::seconds value)
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_lifetime = std::max(value, std::chrono::seconds::zero());
	m_impl->m_expires = clock_t::now() + m_impl->m_lifetime;
	return *this;
}

app_session &app_session::expand(std::chrono::seconds value)
{
	std::lock_guard lock(m_impl->m_mutex);
	if( m_impl->m_valid )
		m_impl->m_expires += std::max(value, std::chrono::seconds::zero());
	return *this;
}

app_session &app_session::set_timeout_handler(timeout_handler_t handler)
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_timeout_handler = std::move(handler);
	return *this;
}

app_session &app_session::unbind_timeout() noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_timeout_handler = {};
	return *this;
}

app_session &app_session::refresh()
{
	std::lock_guard lock(m_impl->m_mutex);
	if( m_impl->m_valid )
		m_impl->m_expires = clock_t::now() + m_impl->m_lifetime;
	return *this;
}

void app_session::invalidate() noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_valid = false;
}

std::string_view app_session::id() const noexcept
{
	return m_impl->m_id;
}

auto app_session::create_time() const noexcept -> time_point_t
{
	return m_impl->m_created;
}

auto app_session::expires_at() const noexcept -> time_point_t
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_expires;
}

std::chrono::seconds app_session::lifecycle() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_lifetime;
}

bool app_session::is_valid() const noexcept
{
	std::lock_guard lock(m_impl->m_mutex);
	return m_impl->m_valid and clock_t::now() < m_impl->m_expires;
}

void app_session::set_attribute_impl(std::string key, std::any value)
{
	std::lock_guard lock(m_impl->m_mutex);
	m_impl->m_attributes.insert_or_assign(std::move(key), std::move(value));
}

void app_session::erase_attribute_impl(const std::string &key) noexcept
{
	try {
		std::lock_guard lock(m_impl->m_mutex);
		m_impl->m_attributes.erase(key);
	}
	catch(...) {}
}

optional<std::any> app_session::attribute_impl(const std::string &key) const
{
	std::lock_guard lock(m_impl->m_mutex);
	const auto found = m_impl->m_attributes.find(key);

	if( found == m_impl->m_attributes.end() )
		return {};

	return found->second;
}

void app_session::notify_timeout() noexcept
{
	timeout_handler_t handler {};
	{
		std::lock_guard lock(m_impl->m_mutex);
		if( m_impl->m_timeout_notified )
			return ;

		m_impl->m_timeout_notified = true;
		m_impl->m_valid = false;

		handler = m_impl->m_timeout_handler;
	}
	try {
		if( handler )
			handler(*this);
	}
	catch(...) {}
}

} //namespace riwo::http_exp
