// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_SESSION_MANAGER_H
#define RIWO_HTTP_SERVER_SESSION_MANAGER_H

#include <riwo/http/server/session.h>
#include <riwo/core/shared_mutex.h>

namespace riwo::http
{

class RIWO_HTTP_API session_manager
{
	RIWO_DISABLE_COPY(session_manager)

public:
	session_manager();
	~session_manager();

	session_manager(session_manager &&other) noexcept;
	session_manager &operator=(session_manager &&other) noexcept;

public: // Fucking msvc !!!
	template <typename Session, typename...Args>
	[[nodiscard]] std::shared_ptr<Session> make(Args&&...args) requires
		core_concepts::base_of<Session,session> and
		core_concepts::constructible<Session,Args...>;

	template <typename...Args>
	[[nodiscard]] std::shared_ptr<session> make(Args&&...args) requires
		core_concepts::constructible<session,Args...>;

	template <typename Session, typename...Args>
	[[nodiscard]] std::shared_ptr<Session> get_or_make (
		const core_concepts::text_p<char> auto &id, Args&&...args
	) requires
		core_concepts::base_of<Session,session> and
		core_concepts::constructible<Session,Args...>;

	template <typename...Args>
	[[nodiscard]] std::shared_ptr<session> get_or_make (
		const core_concepts::text_p<char> auto &id, Args&&...args
	) requires
		core_concepts::constructible<session,Args...>;

public: // Fucking msvc !!!
	template <typename Session>
	[[nodiscard]] std::shared_ptr<Session> get (
		const core_concepts::text_p<char> auto &id
	) requires
	core_concepts::base_of<Session,session>;

	template <typename Session>
	[[nodiscard]] std::shared_ptr<Session> get_or (
		const core_concepts::text_p<char> auto &id
	) requires
	core_concepts::base_of<Session,session>;

	[[nodiscard]] std::shared_ptr<session> get (
		const core_concepts::text_p<char> auto &id
	);
	[[nodiscard]] std::shared_ptr<session> get_or (
		const core_concepts::text_p<char> auto &id
	) noexcept;

public:
	template <typename Rep, typename Period>
	session_manager &set_lifecycle(const duration<Rep,Period> &seconds);
	[[nodiscard]] std::chrono::seconds lifecycle() const noexcept;

	session_manager &set_cookie_key(core_concepts::text_p<char> auto &&key);
	[[nodiscard]] std::string_view cookie_key() const noexcept;

public:
	template <core_concepts::callable<session_ptr,error_code> Func>
	session_manager &on_error(Func &&func);
	session_manager &unbind_error();

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo::http
#include <riwo/http/server/detail/session_manager.h>


#endif //RIWO_HTTP_SERVER_SESSION_MANAGER_H
