// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/websocket/detail/stream/close_deadline.h>

namespace riwo::experimental::websocket::detail
{

struct RIWO_DECL_HIDDEN close_deadline::operation
{
	asio::steady_timer timer;
	std::weak_ptr<void> owner {};
	callback_t callback {};

	operation(const asio::any_io_executor &exec,
		std::weak_ptr<void> owner_value, callback_t callback_value
	) :
		timer(exec), owner(std::move(owner_value)), callback(callback_value) {}
};

close_deadline::close_deadline() noexcept = default;

close_deadline::~close_deadline()
{
	stop();
}

bool close_deadline::active() const noexcept
{
	return static_cast<bool>(m_operation);
}

sys_expected<> close_deadline::start(const asio::any_io_executor &exec,
	std::weak_ptr<void> owner, std::chrono::milliseconds timeout, callback_t callback) noexcept
{
	auto result = make_sys_expected();
	if( m_operation )
		return result;
	try {
		auto active_operation = std::make_shared<struct operation>(
			exec, std::move(owner), callback
		);
		active_operation->timer.expires_after (
			std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
		);
		active_operation->timer.async_wait([active = active_operation](error_code error) noexcept
		{
			if( error )
				return ;

			if( auto i_owner = active->owner.lock() )
				active->callback(i_owner.get());
		});
		m_operation = std::move(active_operation);
		return result;
	}
	catch(...)
	{
		stop();
		result.despair(exception_error(std::current_exception()));
	}
	return result;
}

void close_deadline::stop() noexcept
{
	auto active_operation = std::exchange(m_operation, {});
	if( not active_operation )
		return ;
	try {
		ignore_unused(active_operation->timer.cancel());
	}
	catch(...) {}
}

} //namespace riwo::experimental::websocket::detail
