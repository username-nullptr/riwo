// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_APP_SESSION_MANAGER_H
#define RIWO_HTTP_EXP_SERVER_APP_SESSION_MANAGER_H

#include <riwo/http_exp/server/app_session.h>
#include <riwo/http_exp/protocol/cookie.h>

namespace riwo::http_exp
{

struct app_session_config
{
	std::chrono::seconds lifetime {60};

	std::string cookie_name {"RIWO_SESSION"};
	std::string cookie_path {"/"};

	bool cookie_secure = false;
	bool cookie_http_only = true;

	cookie_same_site cookie_same_site_value = cookie_same_site::lax;
};

class RIWO_HTTP_EXP_API app_session_manager
{
	RIWO_DISABLE_COPY(app_session_manager)

public:
	explicit app_session_manager(app_session_config config = {});
	~app_session_manager();

	app_session_manager(app_session_manager&&) noexcept;
	app_session_manager &operator=(app_session_manager&&) noexcept;

public:
	[[nodiscard]] app_session_ptr make();
	[[nodiscard]] app_session_ptr get(core_concepts::string_p<char> auto &&id);

	[[nodiscard]] app_session_ptr get_or_make(core_concepts::string_p<char> auto &&id);
	bool erase(core_concepts::string_p<char> auto &&id) noexcept;

	template <typename Session, typename...Args>
	[[nodiscard]] std::shared_ptr<Session> make(Args&&...args) requires
		std::derived_from<Session,app_session> and std::constructible_from<Session,Args...>;

	template <typename Session, typename...Args>
	[[nodiscard]] std::shared_ptr<Session> get_or_make
	(core_concepts::string_p<char> auto &&id, Args&&...args) requires
		std::derived_from<Session,app_session> and std::constructible_from<Session,Args...>;

public:
	void set_config(app_session_config config);
	[[nodiscard]] app_session_config config() const;

	size_t purge_expired() noexcept;
	[[nodiscard]] size_t size() const noexcept;

private:
	void insert(app_session_ptr value);

	[[nodiscard]] app_session_ptr get_impl(const std::string &id);
	[[nodiscard]] bool erase_impl(const std::string &id) noexcept;

	class impl;
	std::unique_ptr<impl> m_impl {};
};

using app_session_manager_ptr = std::shared_ptr<app_session_manager>;

} //namespace riwo::http_exp
#include <riwo/http_exp/server/detail/app_session_manager.h>


#endif //RIWO_HTTP_EXP_SERVER_APP_SESSION_MANAGER_H
