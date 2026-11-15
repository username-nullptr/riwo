// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_DETAIL_FIELDS_H
#define RIWO_HTTP_PROTOCOL_DETAIL_FIELDS_H

namespace riwo::http
{

fields &fields::add(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	return add_impl(strtls::to_string(std::forward<decltype(name)>(name)), std::move(value));
}

fields &fields::set(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	return set_impl(strtls::to_string(std::forward<decltype(name)>(name)), std::move(value));
}

fields &fields::erase(const core_concepts::text_p<char> auto &name)
{
	return erase_impl(strtls::to_view(name));
}

field_range fields::find_all(const core_concepts::text_p<char> auto &name) const
{
	return find_all_impl(strtls::to_string(name));
}

optional<std::string_view> fields::find_first(const core_concepts::text_p<char> auto &name) const noexcept
{
	return find_first_impl(strtls::to_view(name));
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_DETAIL_FIELDS_H
