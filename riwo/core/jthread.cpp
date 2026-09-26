// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HAS_STD_JTHREAD
# define RIWO_HAS_STD_JTHREAD  0
#endif //RIWO_HAS_STD_JTHREAD

#include "jthread.h"

#if !RIWO_HAS_STD_JTHREAD

namespace riwo { namespace detail
{

stop_callback_base::stop_callback_base(execute_t execute) noexcept :
	execute(execute)
{

}

class RIWO_DECL_HIDDEN stop_state::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl() = default;

	void unlink(stop_callback_base &callback) noexcept
	{
		if( callback.previous )
			callback.previous->next = callback.next;
		else
			m_callbacks_head = callback.next;

		if( callback.next )
			callback.next->previous = callback.previous;
		else
			m_callbacks_tail = callback.previous;

		callback.previous = nullptr;
		callback.next = nullptr;
		callback.registered = false;
	}

public:
	std::atomic_bool m_stop_requested {false};
	std::atomic_size_t m_source_count {1};

	std::mutex m_mutex;
	std::condition_variable m_callbacks_done;

	stop_callback_base *m_callbacks_head = nullptr;
	stop_callback_base *m_callbacks_tail = nullptr;
	stop_callback_base *m_executing = nullptr;

	std::thread::id m_executing_thread {};
};

stop_state::stop_state() :
	m_impl(new impl())
{

}

stop_state::~stop_state()
{
	delete m_impl;
}

bool stop_state::stop_requested() const noexcept
{
	return m_impl->m_stop_requested.load(std::memory_order_acquire);
}

bool stop_state::stop_possible() const noexcept
{
	return stop_requested() or m_impl->m_source_count.load(std::memory_order_acquire) != 0;
}

void stop_state::add_source() noexcept
{
	m_impl->m_source_count.fetch_add(1, std::memory_order_relaxed);
}

void stop_state::release_source() noexcept
{
	m_impl->m_source_count.fetch_sub(1, std::memory_order_release);
}

bool stop_state::request_stop() noexcept
{
	std::unique_lock lock(m_impl->m_mutex); RIWO_UNUSED(lock);
	if( m_impl->m_stop_requested.exchange(true, std::memory_order_acq_rel) )
		return false;

	while( m_impl->m_callbacks_head )
	{
		auto *callback = m_impl->m_callbacks_head;
		m_impl->unlink(*callback);

		m_impl->m_executing = callback;
		m_impl->m_executing_thread = std::this_thread::get_id();

		lock.unlock();
		callback->execute(callback);
		lock.lock();

		m_impl->m_executing = nullptr;
		m_impl->m_executing_thread = {};
		m_impl->m_callbacks_done.notify_all();
	}
	return true;
}

void stop_state::register_callback(stop_callback_base &callback) noexcept
{
	std::unique_lock lock(m_impl->m_mutex); RIWO_UNUSED(lock);
	if( m_impl->m_stop_requested.load(std::memory_order_acquire) )
	{
		lock.unlock();
		callback.execute(&callback);
		return ;
	}
	callback.previous = m_impl->m_callbacks_tail;
	callback.next = nullptr;
	callback.registered = true;

	if( m_impl->m_callbacks_tail )
		m_impl->m_callbacks_tail->next = &callback;
	else
		m_impl->m_callbacks_head = &callback;

	m_impl->m_callbacks_tail = &callback;
}

void stop_state::unregister_callback(stop_callback_base &callback) noexcept
{
	std::unique_lock lock(m_impl->m_mutex); RIWO_UNUSED(lock);
	if( callback.registered )
	{
		m_impl->unlink(callback);
		return ;
	}
	if( m_impl->m_executing == &callback and
		m_impl->m_executing_thread != std::this_thread::get_id() )
	{
		m_impl->m_callbacks_done.wait(lock, [&]{
			return m_impl->m_executing != &callback;
		});
	}
}

} //namespace detail

stop_token::stop_token(std::shared_ptr<detail::stop_state> state) noexcept :
	m_state(std::move(state))
{

}

bool stop_token::stop_requested() const noexcept
{
	return m_state and m_state->stop_requested();
}

bool stop_token::stop_possible() const noexcept
{
	return m_state and m_state->stop_possible();
}

void stop_token::swap(stop_token &other) noexcept
{
	m_state.swap(other.m_state);
}

stop_source::stop_source() :
	m_state(std::make_shared<detail::stop_state>())
{

}

stop_source::stop_source(nostopstate_t) noexcept
{

}

stop_source::stop_source(const stop_source &other) noexcept :
	m_state(other.m_state)
{
	add_source();
}

stop_source::~stop_source()
{
	release_source();
}

stop_source &stop_source::operator=(const stop_source &other) noexcept
{
	if( this != &other )
	{
		stop_source copy(other);
		swap(copy);
	}
	return *this;
}

stop_source &stop_source::operator=(stop_source &&other) noexcept
{
	if( this != &other )
	{
		release_source();
		m_state = std::move(other.m_state);
	}
	return *this;
}

stop_token stop_source::get_token() const noexcept
{
	return stop_token(m_state);
}

bool stop_source::stop_possible() const noexcept
{
	return static_cast<bool>(m_state);
}

bool stop_source::stop_requested() const noexcept
{
	return m_state and m_state->stop_requested();
}

bool stop_source::request_stop() noexcept
{
	const auto state = m_state;
	return state and state->request_stop();
}

void stop_source::swap(stop_source &other) noexcept
{
	m_state.swap(other.m_state);
}

void stop_source::add_source() noexcept
{
	if( m_state )
		m_state->add_source();
}

void stop_source::release_source() noexcept
{
	if( m_state )
		m_state->release_source();
}

jthread::~jthread()
{
	if( joinable() )
	{
		request_stop();
		join();
	}
}

jthread &jthread::operator=(jthread &&other) noexcept
{
	if( this != &other )
	{
		if( joinable() )
		{
			request_stop();
			join();
		}
		m_stop_source = std::move(other.m_stop_source);
		m_thread = std::move(other.m_thread);
	}
	return *this;
}

void jthread::swap(jthread &other) noexcept
{
	m_stop_source.swap(other.m_stop_source);
	m_thread.swap(other.m_thread);
}

bool jthread::joinable() const noexcept
{
	return m_thread.joinable();
}

void jthread::join()
{
	m_thread.join();
}

void jthread::detach()
{
	m_thread.detach();
}

jthread::id jthread::get_id() const noexcept
{
	return m_thread.get_id();
}

jthread::native_handle_type jthread::native_handle()
{
	return m_thread.native_handle();
}

stop_source jthread::get_stop_source() noexcept
{
	return m_stop_source;
}

stop_token jthread::get_stop_token() const noexcept
{
	return m_stop_source.get_token();
}

bool jthread::request_stop() noexcept
{
	return m_stop_source.request_stop();
}

unsigned int jthread::hardware_concurrency() noexcept
{
	return std::thread::hardware_concurrency();
}

void swap(stop_token &left, stop_token &right) noexcept
{
	left.swap(right);
}

void swap(stop_source &left, stop_source &right) noexcept
{
	left.swap(right);
}

void swap(jthread &left, jthread &right) noexcept
{
	left.swap(right);
}

} //namespace riwo

#endif //RIWO_HAS_STD_JTHREAD
