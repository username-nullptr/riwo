// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/coro/detail/wake_up.h>

namespace riwo::coro::detail
{

class RIWO_DECL_HIDDEN lock_wake_up::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl(asio::any_io_executor exec, handler_t handler) :
		m_exec(std::move(exec)), m_handler(std::move(handler)) {}

	asio::any_io_executor m_exec {};
	handler_t m_handler;

	optional<asio::steady_timer> m_timer {};
	std::atomic_flag m_finished {};
};

lock_wake_up::lock_wake_up(asio::any_io_executor exec, handler_t handler) :
	m_impl(new impl(std::move(exec), std::move(handler)))
{

}

lock_wake_up::~lock_wake_up()
{
	delete m_impl;
}

bool lock_wake_up::operator()(bool success)
{
	if( m_impl->m_finished.test_and_set(std::memory_order_acq_rel) )
		return false;

	if( m_impl->m_timer )
		m_impl->m_timer->cancel();

	// Keep long waiter hand-off chains from recursively growing the stack.
	asio::post(m_impl->m_exec,
	[success, handler = std::move(m_impl->m_handler)]() mutable {
		std::move(handler)(success);
	});
	return true;
}

asio::any_io_executor lock_wake_up::get_executor() noexcept
{
	return m_impl->m_exec;
}

optional<asio::steady_timer> &lock_wake_up::timer() noexcept
{
	return m_impl->m_timer;
}

} //namespace riwo::coro::detail
