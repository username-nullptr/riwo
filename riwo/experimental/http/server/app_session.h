// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_APP_SESSION_H
#define RIWO_EXP_HTTP_SERVER_APP_SESSION_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

class app_session_manager;

class RIWO_EXP_HTTP_API app_session :
	public std::enable_shared_from_this<app_session>
{
	RIWO_DISABLE_COPY_MOVE(app_session)
	friend class app_session_manager;

public:
	using clock_t = std::chrono::steady_clock;
	using time_point_t = clock_t::time_point;

	using timeout_handler_t = std::function<void(app_session&)>;
	using ptr_t = std::shared_ptr<app_session>;

	explicit app_session (
		std::chrono::seconds lifetime = std::chrono::seconds(60)
	);
	virtual ~app_session();

public:
	app_session &set_attribute(core_concepts::text_p<char> auto &&key, std::any value);
	app_session &erase_attribute(core_concepts::text_p<char> auto &&key) noexcept;
	void clear_attributes() noexcept;

	app_session &set_lifecycle(std::chrono::seconds value);
	app_session &expand(std::chrono::seconds value);

	app_session &set_timeout_handler(timeout_handler_t handler);
	app_session &unbind_timeout() noexcept;

	app_session &refresh();
	void invalidate() noexcept;

public:
	[[nodiscard]] std::string_view id() const noexcept;
	[[nodiscard]] time_point_t create_time() const noexcept;

	[[nodiscard]] time_point_t expires_at() const noexcept;
	[[nodiscard]] std::chrono::seconds lifecycle() const noexcept;

	template <typename Value>
	[[nodiscard]] optional<Value> attribute_as(core_concepts::text_p<char> auto &&key) const;
	[[nodiscard]] optional<std::any> attribute(core_concepts::text_p<char> auto &&key) const;

	[[nodiscard]] bool is_valid() const noexcept;

private:
	void set_attribute_impl(std::string key, std::any value);
	void erase_attribute_impl(const std::string &key) noexcept;

	[[nodiscard]] optional<std::any> attribute_impl(const std::string &key) const;
	void notify_timeout() noexcept;

	class impl;
	std::unique_ptr<impl> m_impl {};
};

using app_session_ptr = app_session::ptr_t;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/server/detail/app_session.h>


#endif //RIWO_EXP_HTTP_SERVER_APP_SESSION_H
