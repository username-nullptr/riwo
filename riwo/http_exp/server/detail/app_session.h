// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_APP_SESSION_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_APP_SESSION_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp
{

app_session &app_session::set_attribute(core_concepts::text_p<char> auto &&key, std::any value)
{
	set_attribute_impl (
		strtls::to_string(std::forward<decltype(key)>(key)), std::move(value)
	);
	return *this;
}

app_session &app_session::erase_attribute(core_concepts::text_p<char> auto &&key) noexcept
{
	erase_attribute_impl (
		strtls::to_string(std::forward<decltype(key)>(key))
	);
	return *this;
}

template <typename Value>
optional<Value> app_session::attribute_as(core_concepts::text_p<char> auto &&key) const
{
	auto value = attribute(std::forward<decltype(key)>(key));
	if( not value )
		return {};

	if( const auto *result = std::any_cast<Value>(&*value) )
		return *result;
	return {};
}

optional<std::any> app_session::attribute(core_concepts::text_p<char> auto &&key) const
{
	return attribute_impl (
		strtls::to_string(std::forward<decltype(key)>(key))
	);
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SERVER_DETAIL_APP_SESSION_H
