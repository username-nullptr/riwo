// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session_manager.h"

namespace riwo::http
{

session_ptr session_manager::impl::find(std::string_view id, bool _throw)
{
	session_ptr session;
	{
		std::shared_lock locker(m_map_mutex); RIWO_UNUSED(locker);
		auto it = m_session_map.find(id);

		if( it == m_session_map.end() )
		{
			if( _throw )
			{
				runtime_error::loc_throw(std::format (
					"riwo::http::session_manager: <map>: id '{}' not exists.", id
				));
			}
			return {};
		}
		session = it->second;
	}
	session->expand();
	return session;
}

std::pair<std::map<std::string_view,session_ptr>::iterator,bool>
session_manager::impl::emplace(session_ptr session)
{
	std::unique_lock locker(m_map_mutex); RIWO_UNUSED(locker);
	return m_session_map.emplace(session->id(), std::move(session));
}

void session_manager::impl::erase(std::string_view id)
{
	std::unique_lock locker(m_map_mutex); RIWO_UNUSED(locker);
	m_session_map.erase(std::string(id.data(), id.size()));
}

session_manager::session_manager() :
	m_impl(new impl())
{

}

session_manager::~session_manager()
{
	delete m_impl;
}

session_manager::session_manager(session_manager &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl();
}

session_manager &session_manager::operator=(session_manager &&other) noexcept
{
	if( this == &other )
		return *this;
	delete m_impl;
	m_impl = other.m_impl;
	other.m_impl = new impl();
	return *this;
}

std::chrono::seconds session_manager::lifecycle() const noexcept
{
	return { m_impl->m_lifecycle };
}

std::string_view session_manager::cookie_key() const noexcept
{
	return m_impl->m_cookie_key;
}

session_manager &session_manager::unbind_error()
{
	m_impl->m_error_handle = nullptr;
	return *this;
}

} //namespace riwo::http
