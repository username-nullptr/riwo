// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/automatic_ping.h>

namespace riwo::websocket::detail
{

automatic_ping::automatic_ping(const asio::any_io_executor &exec, std::weak_ptr<void> owner,
	std::chrono::milliseconds interval, size_t timeout_retries, write_fn_t write_fn, fail_fn_t fail_fn) :
	m_timer(exec), m_owner(std::move(owner)), m_interval(interval),
	m_timeout_retries(timeout_retries), m_write_fn(write_fn), m_fail_fn(fail_fn)
{

}

automatic_ping::~automatic_ping() = default;

sys_expected<std::shared_ptr<automatic_ping>>
automatic_ping::create(const asio::any_io_executor &exec, std::weak_ptr<void> owner,
	std::chrono::milliseconds interval, size_t timeout_retries, write_fn_t write_fn, fail_fn_t fail_fn) noexcept
{
	sys_expected<std::shared_ptr<automatic_ping>> result;
	try {
		result = std::make_shared<automatic_ping>(exec,
			std::move(owner), interval, timeout_retries, write_fn, fail_fn
		);
		if( auto started = (*result)->start(); not started )
			return sys_unexpected(started.error());
		return result;
	}
	catch(...) {
		result.despair(exception_error(std::current_exception()));
	}
	return result;
}

sys_expected<> automatic_ping::start() noexcept
{
	m_active = true;
	return schedule();
}

sys_expected<> automatic_ping::schedule() noexcept
{
	auto result = make_sys_expected();
	if( not m_active )
		return result;
	try {
		m_timer.expires_after(m_interval);
		m_timer.async_wait([self = shared_from_this()](error_code error) {
			self->timer_completed(error);
		});
		return result;
	}
	catch(...) {
		result.despair(exception_error(std::current_exception()));
	}
	return result;
}

void automatic_ping::timer_completed(error_code error) noexcept
{
	if( error or not m_active )
		return ;

	auto owner = m_owner.lock();
	if( not owner )
	{
		stop();
		return ;
	}
	try {
		if( m_awaited_pong )
		{
			if( m_consecutive_timeouts >= m_timeout_retries )
			{
				report_failure(asio::error::timed_out);
				return ;
			}
			++m_consecutive_timeouts;
		}
		auto payload = std::make_shared<payload_t>();
		auto id = ++m_next_ping_id;

		for(size_t index=0; index<payload->size(); ++index)
		{
			(*payload)[payload->size() - index - 1] = static_cast<std::byte>(id & 0xFF);
			id >>= 8;
		}
		m_awaited_pong = *payload;
		auto self = shared_from_this();

		io_handler_t handler (
		[self = std::move(self), payload](error_code write_error, size_t) mutable
		{
			RIWO_UNUSED(payload);
			self->write_completed(write_error);
		});
		const auto body = const_buffer(payload->data(), payload->size());
		m_write_fn(owner.get(), body, std::move(handler));
	}
	catch(...) {
		report_failure(exception_error(std::current_exception()));
	}
}

void automatic_ping::write_completed(error_code error) noexcept
{
	if( error or not m_active )
		return ;

	if( auto scheduled = schedule(); not scheduled )
		report_failure(scheduled.error());
}

void automatic_ping::acknowledge(std::span<const std::byte> payload) noexcept
{
	if( not m_awaited_pong or payload.size() != m_awaited_pong->size() or
		not std::equal(payload.begin(), payload.end(), m_awaited_pong->begin()) )
		return ;

	m_awaited_pong.reset();
	m_consecutive_timeouts = 0;
}

void automatic_ping::report_failure(error_code error) noexcept
{
	if( not m_active )
		return ;
	m_active = false;

	if( auto owner = m_owner.lock() )
		m_fail_fn(owner.get(), error);
}

void automatic_ping::stop() noexcept
{
	m_active = false;
	m_awaited_pong.reset();
	m_consecutive_timeouts = 0;
	try {
		ignore_unused(m_timer.cancel());
	}
	catch(...) {}
}

} //namespace riwo::websocket::detail
