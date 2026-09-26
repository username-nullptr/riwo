// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_SESSION_H
#define RIWO_HTTP_SERVER_SESSION_H

#include <riwo/http/protocol/types.h>
#include <riwo/core/execution.h>

namespace riwo::http
{

class RIWO_HTTP_API session : public std::enable_shared_from_this<session>
{
	RIWO_DISABLE_COPY_MOVE(session)

public:
	using executor_type = asio::any_io_executor;
	using executor_t = executor_type;

	template <typename Rep, typename Period = std::ratio<1>>
	using duration_t = std::chrono::duration<Rep,Period>;

	using sys_clock_t = std::chrono::steady_clock;
	using time_point_t = sys_clock_t::time_point;

	using value_t = riwo::value;
	using attributes_t = map<std::any>;

public:
	template <typename Rep, typename Period = std::ratio<1>>
	explicit session(const duration_t<Rep,Period> &seconds,
		const executor_t &exec = riwo::get_executor()
	);
	explicit session(const executor_t &exec = riwo::get_executor());
	virtual ~session();

public:
	[[nodiscard]] executor_t get_executor() noexcept;
	[[nodiscard]] std::string_view id() const noexcept;

	[[nodiscard]] time_point_t create_time() const noexcept;
	[[nodiscard]] bool is_valid() const noexcept;

public:
	[[nodiscard]] optional<std::any> attribute (
		const core_concepts::text_p<char> auto &key
	) const noexcept;

	session &set_attribute (
		core_concepts::text_p<char> auto &&key, std::any value
	) noexcept;

	session &unset_attribute (
		const core_concepts::text_p<char> auto &key
	) noexcept;

	[[nodiscard]] const attributes_t &attributes() const noexcept;
	[[nodiscard]] attributes_t &attributes() noexcept;

public:
	[[nodiscard]] std::chrono::seconds lifecycle() const noexcept;
	void invalidate();

	template <typename Rep, typename Period = std::ratio<1>>
	session &set_lifecycle(const duration_t<Rep,Period> &seconds);

	template <typename Rep, typename Period = std::ratio<1>>
	session &expand(const duration_t<Rep,Period> &seconds);
	session &expand();

public:
	template <core_concepts::callable Func>
	session &on_timeout(Func &&func);

	template <core_concepts::callable<error_code> Func>
	session &on_error(Func &&func);

	session &unbind_timeout();
	session &unbind_error();

private:
	class impl;
	impl *m_impl;
};

using session_attributes = session::attributes_t;
using session_ptr = std::shared_ptr<session>;

} //namespace riwo::http
#include <riwo/http/server/detail/session.h>


#endif //RIWO_HTTP_SERVER_SESSION_H
