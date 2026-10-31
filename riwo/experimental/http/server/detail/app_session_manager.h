// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_DETAIL_APP_SESSION_MANAGER_H
#define RIWO_EXP_HTTP_SERVER_DETAIL_APP_SESSION_MANAGER_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

app_session_ptr app_session_manager::get(core_concepts::string_p<char> auto &&id)
{
	if constexpr( std::same_as<std::remove_cvref_t<decltype(id)>,char> )
		return get_impl(std::string(1,id));
	else
		return get_impl(strtls::to_string(std::forward<decltype(id)>(id)));
}

app_session_ptr app_session_manager::get_or_make(core_concepts::string_p<char> auto &&id)
{
	if( auto current = get(std::forward<decltype(id)>(id)) )
	{
		current->refresh();
		return current;
	}
	return make();
}

bool app_session_manager::erase(core_concepts::string_p<char> auto &&id) noexcept
{
	try {
		if constexpr( std::same_as<std::remove_cvref_t<decltype(id)>,char> )
			return erase_impl(std::string(1,id));
		else
			return erase_impl(strtls::to_string(std::forward<decltype(id)>(id)));
	}
	catch(...) {}
	return false;
}

template <typename Session, typename...Args>
std::shared_ptr<Session> app_session_manager::make(Args&&...args) requires
	std::derived_from<Session,app_session> and std::constructible_from<Session,Args...>
{
	auto value = std::make_shared<Session>(std::forward<Args>(args)...);
	insert(value);
	return value;
}

template <typename Session, typename...Args>
std::shared_ptr<Session> app_session_manager::get_or_make
(core_concepts::string_p<char> auto &&id, Args&&...args) requires
	std::derived_from<Session,app_session> and std::constructible_from<Session,Args...>
{
	if( auto current = std::dynamic_pointer_cast<Session>(get(std::forward<decltype(id)>(id))) )
		return current;
	return make<Session>(std::forward<Args>(args)...);
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_SERVER_DETAIL_APP_SESSION_MANAGER_H
