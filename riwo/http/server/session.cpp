// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"

namespace riwo::http
{

void session::impl::start()
{
	if( m_restart )
		m_timer.cancel();
	else if( not m_valid )
	{
		m_valid = true;
		dispatch(m_timer.get_executor(), work());
	}
}

awaitable<void> session::impl::work()
{
	auto self = q_ptr->shared_from_this();
	error_code error;
	for(;;)
	{
		self->m_impl->m_restart = false;
		self->m_impl->m_timer.expires_after(std::chrono::seconds(self->m_impl->m_second));

		using namespace riwo::operators;
		co_await self->m_impl->m_timer.async_wait(use_awaitable|error);
		if( self.use_count() == 1 )
			break;

		if( error and error != errc::operation_aborted )
		{
			if( self->m_impl->m_error_handle )
				self->m_impl->m_error_handle(error);
		}
		if( self->m_impl->m_restart )
			continue;

		self->m_impl->m_valid = false;
		if( self->m_impl->m_timeout_handle )
			self->m_impl->m_timeout_handle();
		break;
	}
	co_return ;
}

session::session(const executor_t &exec) :
	session(std::chrono::seconds(60), exec)
{

}

session::~session()
{
	delete m_impl;
}

auto session::get_executor() noexcept -> executor_t
{
	return m_impl->m_timer.get_executor();
}

std::string_view session::id() const noexcept
{
	return m_impl->m_id;
}

auto session::create_time() const noexcept -> time_point_t
{
	return m_impl->m_create_time;
}

bool session::is_valid() const noexcept
{
	return m_impl->m_valid;
}

auto session::attributes() const noexcept -> const attributes_t&
{
	return m_impl->m_attributes;
}

auto session::attributes() noexcept -> attributes_t&
{
	return m_impl->m_attributes;
}

std::chrono::seconds session::lifecycle() const noexcept
{
	return std::chrono::seconds(m_impl->m_second);
}

void session::invalidate()
{
	m_impl->m_valid = false;
	m_impl->m_timer.cancel();
}

session &session::expand()
{
	m_impl->m_restart = true;
	m_impl->start();
	return *this;
}

session &session::unbind_timeout()
{
	m_impl->m_timeout_handle = nullptr;
	return *this;
}

session &session::unbind_error()
{
	m_impl->m_error_handle = nullptr;
	return *this;
}

} //namespace riwo::http
