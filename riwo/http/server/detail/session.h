// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_SESSION_H
#define RIWO_HTTP_SERVER_DETAIL_SESSION_H

#include <riwo/core/algorithm/uuid.h>

namespace riwo::http
{

class RIWO_HTTP_API session::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	template <typename Rep, typename Period = std::ratio<1>>
	impl(session *owner, const duration<Rep,Period> &seconds, const executor_t &exec) :
		q_ptr(owner), m_second(static_cast<uint64_t>(seconds.count())), m_timer(exec) {}

	void start();

private:
	[[nodiscard]] awaitable<void> work();

public:
	session *q_ptr = nullptr;
	const std::string m_id = uuid::generate();
	time_point_t m_create_time = sys_clock_t::now();

	attributes_t m_attributes {};
	std::atomic<uint64_t> m_second;

	std::atomic_bool m_valid = false;
	std::atomic_bool m_restart = false;

	asio::steady_timer m_timer;
	std::function<void()> m_timeout_handle {};
	std::function<void(error_code)> m_error_handle {};
};

template <typename Rep, typename Period>
session::session(const duration<Rep,Period> &seconds, const executor_t &exec) :
	m_impl(new impl(this, seconds, exec))
{
	m_impl->start();
}

optional<std::any> session::attribute(const core_concepts::text_p<char> auto &key) const noexcept
{
	auto it = m_impl->m_attributes.find(strtls::to_string(key));
	return it == m_impl->m_attributes.end() ?
		optional<std::any>() : optional<std::any>(it->second);
}

session &session::set_attribute(core_concepts::text_p<char> auto &&key, std::any value) noexcept
{
	m_impl->m_attributes[strtls::to_string(std::forward<decltype(key)>(key))] = std::move(value);
	return *this;
}

session &session::unset_attribute(const core_concepts::text_p<char> auto &key) noexcept
{
	m_impl->m_attributes.erase(strtls::to_string(key));
	return *this;
}

template <typename Rep, typename Period>
session &session::set_lifecycle(const duration<Rep,Period> &seconds)
{
	namespace sc = std::chrono;
	m_impl->m_second = static_cast<uint64_t>(
		sc::duration_cast<sc::seconds>(seconds).count()
	);
	if( m_impl->m_second == 0 )
		m_impl->m_second = 1;

	m_impl->m_restart = true;
	m_impl->start();
	return *this;
}

template <typename Rep, typename Period>
session &session::expand(const duration<Rep,Period> &seconds)
{
	namespace sc = std::chrono;
	m_impl->m_second += static_cast<uint64_t>(
		sc::duration_cast<sc::seconds>(seconds).count()
	);
	return expand();
}

template <core_concepts::callable Func>
session &session::on_timeout(Func &&func)
{
	m_impl->m_timeout_handle = std::forward<Func>(func);
	return *this;
}

template <core_concepts::callable<error_code> Func>
session &session::on_error(Func &&func)
{
	m_impl->m_error_handle = std::forward<Func>(func);
	return *this;
}

} //namespace riwo::http


#endif //RIWO_HTTP_SERVER_DETAIL_SESSION_H
