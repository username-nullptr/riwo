// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "app_session_manager.h"

namespace riwo::experimental::http
{

class RIWO_DECL_HIDDEN app_session_manager::impl
{
public:
	explicit impl(app_session_config value) :
		config(std::move(value)) {}

	mutable std::mutex mutex {};
	app_session_config config {};
	std::unordered_map<std::string,app_session_ptr> sessions {};
};

app_session_manager::app_session_manager(app_session_config config) :
	m_impl(std::make_unique<impl>(std::move(config)))
{

}

app_session_manager::~app_session_manager() = default;

app_session_manager::app_session_manager(app_session_manager &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(app_session_config{});
}

app_session_manager &app_session_manager::operator=(app_session_manager &&other) noexcept
{
	if( this == &other )
		return *this;

	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(app_session_config{});
	return *this;
}

app_session_ptr app_session_manager::make()
{
	app_session_config current;
	{
		std::lock_guard lock(m_impl->mutex);
		current = m_impl->config;
	}
	auto result = std::make_shared<app_session>(current.lifetime);
	insert(result);
	return result;
}

void app_session_manager::set_config(app_session_config config)
{
	std::lock_guard lock(m_impl->mutex);
	m_impl->config = std::move(config);
}

app_session_config app_session_manager::config() const
{
	std::lock_guard lock(m_impl->mutex);
	return m_impl->config;
}

size_t app_session_manager::purge_expired() noexcept
{
	std::vector<app_session_ptr> expired {};
	{
		std::lock_guard lock(m_impl->mutex);
		for(auto iterator=m_impl->sessions.begin(); iterator!=m_impl->sessions.end(); )
		{
			if( iterator->second->is_valid() )
				++iterator;
			else
			{
				expired.emplace_back(iterator->second);
				iterator = m_impl->sessions.erase(iterator);
			}
		}
	}
	for(const auto &value : expired)
		value->notify_timeout();
	return expired.size();
}

size_t app_session_manager::size() const noexcept
{
	std::lock_guard lock(m_impl->mutex);
	return m_impl->sessions.size();
}

void app_session_manager::insert(app_session_ptr value)
{
	if( not value )
		invalid_argument::loc_throw("riwo::experimental::http::app_session_manager: null session");

	std::lock_guard lock(m_impl->mutex);
	m_impl->sessions.insert_or_assign(std::string(value->id()), std::move(value));
}

app_session_ptr app_session_manager::get_impl(const std::string &id)
{
	if( id.empty() )
		return {};

	app_session_ptr expired {};
	{
		std::lock_guard lock(m_impl->mutex);
		const auto found = m_impl->sessions.find(id);

		if( found == m_impl->sessions.end() )
			return {};

		if( found->second->is_valid() )
			return found->second;

		expired = found->second;
		m_impl->sessions.erase(found);
	}
	expired->notify_timeout();
	return {};
}

bool app_session_manager::erase_impl(const std::string &id) noexcept
{
	try {
		std::lock_guard lock(m_impl->mutex);
		return m_impl->sessions.erase(id) != 0;
	}
	catch(...) {}
	return false;
}

} //namespace riwo::experimental::http
