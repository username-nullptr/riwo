// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_DETAIL_COOKIE_H
#define RIWO_HTTP_PROTOCOL_DETAIL_COOKIE_H

namespace riwo::http
{

template <typename T>
decltype(auto) cookie::value() requires core_concepts::value_get<T,char>
{
	return value().get<T>();
}

cookie &cookie::set_attribute(core_concepts::text_p<char> auto &&key, value_t attr) noexcept
{
	attributes()[strtls::to_string(std::forward<decltype(key)>(key))] = std::move(attr);
	return *this;
}

cookie &cookie::unset_attribute(const core_concepts::text_p<char> auto &key) noexcept
{
	attributes().erase(strtls::to_string(key));
	return *this;
}

optional<value> cookie::attribute(const core_concepts::text_p<char> auto &key) noexcept
{
	return value_map_get(attributes(), key);
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_DETAIL_COOKIE_H
