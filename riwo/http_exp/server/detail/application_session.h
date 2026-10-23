// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_APPLICATION_SESSION_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_APPLICATION_SESSION_H

namespace riwo::http_exp
{

template <typename Value>
optional<Value> application_session::attribute_as(std::string_view key) const
{
	auto value = attribute(key);
	if( not value ) return {};
	if( const auto *result = std::any_cast<Value>(&*value) ) return *result;
	return {};
}

template <typename Session, typename... Args>
std::shared_ptr<Session> application_session_manager::create(Args&&... args)
	requires std::derived_from<Session,application_session> and
		std::constructible_from<Session,Args...>
{
	auto value = std::make_shared<Session>(std::forward<Args>(args)...);
	insert(value);
	return value;
}

template <typename Session, typename... Args>
std::shared_ptr<Session> application_session_manager::get_or_create(
	std::string_view id, Args&&... args)
	requires std::derived_from<Session,application_session> and
		std::constructible_from<Session,Args...>
{
	if( auto current = std::dynamic_pointer_cast<Session>(get(id)) )
		return current;
	return create<Session>(std::forward<Args>(args)...);
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_APPLICATION_SESSION_H
