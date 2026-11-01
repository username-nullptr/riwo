// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/close_deadline.h>

namespace riwo::websocket::detail
{

struct RIWO_DECL_HIDDEN close_deadline::operation
{
	asio::steady_timer timer;
	std::weak_ptr<void> owner;
	callback_t callback;

	operation(const asio::any_io_executor &exec,
		std::weak_ptr<void> owner_value, callback_t callback_value
	) :
		timer(exec), owner(std::move(owner_value)), callback(std::move(callback_value)) {}
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
		auto state = std::make_shared<struct operation>(
			exec, std::move(owner), callback
		);
		state->timer.expires_after (
			std::chrono::duration_cast<asio::steady_timer::duration>(timeout)
		);
		state->timer.async_wait([state](error_code error) noexcept
		{
			if( error )
				return ;

			if( auto _owner = state->owner.lock() )
				state->callback(_owner.get());
		});
		m_operation = std::move(state);
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
	auto state = std::exchange(m_operation, {});
	if( not state )
		return ;
	try {
		ignore_unused(state->timer.cancel());
	}
	catch(...) {}
}

} //namespace riwo::websocket::detail
