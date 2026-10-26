// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_APPLICATION_SESSION_H
#define RIWO_HTTP_EXP_SERVER_APPLICATION_SESSION_H

#include <riwo/http_exp/protocol/cookie.h>
#include <any>
#include <mutex>
#include <unordered_map>

namespace riwo::http_exp
{

struct application_session_config
{
	std::chrono::seconds lifetime {60};
	std::string cookie_name {"RIWO_SESSION"};
	std::string cookie_path {"/"};
	bool cookie_secure = false;
	bool cookie_http_only = true;
	cookie_same_site cookie_same_site_value = cookie_same_site::lax;
};

class RIWO_HTTP_EXP_API application_session :
	public std::enable_shared_from_this<application_session>
{
	RIWO_DISABLE_COPY_MOVE(application_session)

public:
	using clock_t = std::chrono::steady_clock;
	using time_point_t = clock_t::time_point;
	using timeout_handler_t = std::function<void(application_session&)>;

	explicit application_session(std::chrono::seconds lifetime =
		std::chrono::seconds(60));
	virtual ~application_session();

	[[nodiscard]] std::string_view id() const noexcept;
	[[nodiscard]] time_point_t create_time() const noexcept;
	[[nodiscard]] time_point_t expires_at() const noexcept;
	[[nodiscard]] std::chrono::seconds lifecycle() const noexcept;
	[[nodiscard]] bool valid() const noexcept;

	[[nodiscard]] optional<std::any> attribute(
		std::string_view key) const;

	template <typename Value>
	[[nodiscard]] optional<Value> attribute_as(std::string_view key) const;

	application_session &set_attribute(
		core_concepts::text_p<char> auto &&key, std::any value);
	application_session &erase_attribute(std::string_view key) noexcept;
	void clear_attributes() noexcept;

	application_session &set_lifecycle(std::chrono::seconds value);
	application_session &refresh();
	application_session &expand(std::chrono::seconds value);
	void invalidate() noexcept;
	application_session &set_timeout_handler(timeout_handler_t handler);
	application_session &unbind_timeout() noexcept;

private:
	application_session &set_attribute_impl(std::string key, std::any value);

	friend class application_session_manager;
	void notify_timeout() noexcept;

	std::string m_id;
	time_point_t m_created;
	time_point_t m_expires;
	std::chrono::seconds m_lifetime;
	mutable std::mutex m_mutex;
	std::unordered_map<std::string,std::any> m_attributes;
	timeout_handler_t m_timeout_handler;
	bool m_valid = true;
	bool m_timeout_notified = false;
};

using application_session_ptr = std::shared_ptr<application_session>;

class RIWO_HTTP_EXP_API application_session_manager
{
	RIWO_DISABLE_COPY(application_session_manager)

public:
	explicit application_session_manager(application_session_config config = {});
	~application_session_manager();
	application_session_manager(application_session_manager&&) noexcept;
	application_session_manager &operator=(application_session_manager&&) noexcept;

	[[nodiscard]] application_session_config config() const;
	void set_config(application_session_config config);

	[[nodiscard]] application_session_ptr create();
	[[nodiscard]] application_session_ptr get(std::string_view id);
	[[nodiscard]] application_session_ptr get_or_create(std::string_view id);
	bool erase(std::string_view id) noexcept;
	size_t purge_expired() noexcept;
	[[nodiscard]] size_t size() const noexcept;

	template <typename Session, typename... Args>
	[[nodiscard]] std::shared_ptr<Session> create(Args&&... args)
		requires std::derived_from<Session,application_session> and
			std::constructible_from<Session,Args...>;

	template <typename Session, typename... Args>
	[[nodiscard]] std::shared_ptr<Session> get_or_create(
		std::string_view id, Args&&... args)
		requires std::derived_from<Session,application_session> and
			std::constructible_from<Session,Args...>;

private:
	void insert(application_session_ptr value);
	class impl;
	std::unique_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

#include <riwo/http_exp/server/detail/application_session.h>

#endif //RIWO_HTTP_EXP_SERVER_APPLICATION_SESSION_H
