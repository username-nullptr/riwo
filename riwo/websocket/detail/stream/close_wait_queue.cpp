// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/close_wait_queue.h>

namespace riwo::websocket::detail
{

close_wait_queue::close_wait_queue() noexcept = default;

close_wait_queue::~close_wait_queue() = default;

bool close_wait_queue::notified() const noexcept
{
	return m_notified;
}

void close_wait_queue::set_callback(closed_callback callback)
{
	m_callback = std::move(callback);
}

void close_wait_queue::notify(const close_info &result) noexcept
{
	if( m_notified or not m_callback )
		return ;

	m_notified = true;
	auto callback = std::move(m_callback);
	try {
		callback(result);
	}
	catch(...) {}
}

bool close_wait_queue::add
(handler_t completion, asio::any_io_executor exec, std::weak_ptr<void> owner, cancel_fn_t cancel_fn) noexcept
{
	std::shared_ptr<close_wait_operation> waiter;
	try {
		auto associated_allocator = asio::get_associated_allocator(completion);
		using allocator_t = std::allocator_traits
			<decltype(associated_allocator)>::template rebind_alloc<close_wait_operation>;

		waiter = std::allocate_shared<close_wait_operation>(
			allocator_t(associated_allocator)
		);
		waiter->id = ++m_next_id;
		waiter->completion = std::move(completion);

		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
		{
			slot.assign([owner = std::move(owner), exec = std::move(exec), cancel_fn, id = waiter->id]
			(asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto active = owner.lock() )
				{
					try {
						asio::dispatch(exec,
						[active = std::move(active), cancel_fn, id]{
							cancel_fn(active.get(), id);
						});
					}
					catch(...) {}
				}
			});
		}
		m_waiters.push_back(waiter);
		return true;
	}
	catch(...)
	{
		auto error = exception_error(std::current_exception());
		if( waiter and waiter->completion )
			completion = std::move(waiter->completion);
		try {
			post(std::move(completion), exec, error, close_info{});
		}
		catch(...) {}
	}
	return false;
}

void close_wait_queue::post
(handler_t completion, const asio::any_io_executor &exec, error_code error, close_info result)
{
	post_completion(exec,
		std::move(completion), error, std::move(result)
	);
}

void close_wait_queue::cancel(uint64_t id) noexcept
{
	for(auto it=m_waiters.begin(); it!=m_waiters.end(); ++it)
	{
		if( (*it)->id != id )
			continue;

		auto waiter = std::move(*it);
		m_waiters.erase(it);
		try {
			auto completion = std::move(waiter->completion);
			std::move(completion)(asio::error::operation_aborted, close_info{});
		}
		catch(...) {}
		return ;
	}
}

void close_wait_queue::complete(error_code error, const close_info &result) noexcept
{
	notify(result);
	while( not m_waiters.empty() )
	{
		auto waiter = std::move(m_waiters.front());
		m_waiters.pop_front();

		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
			slot.clear();
		try {
			auto completion = std::move(waiter->completion);
			std::move(completion)(error, result);
		}
		catch(...) {}
	}
}

} //namespace riwo::websocket::detail
