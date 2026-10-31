// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_RESPONSE_HEAD_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_RESPONSE_HEAD_H

namespace riwo::experimental::http
{

optional<std::string_view> response_head::header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return headers.find_first(name);
}

bool response_head::has_header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return headers.find_first(name).has_value();
}

bool response_head::contains_header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return has_header(name);
}

response_head &response_head::set_header
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	headers.set(std::forward<decltype(name)>(name), std::move(value));
	return *this;
}

response_head &response_head::add_header
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	headers.add(std::forward<decltype(name)>(name), std::move(value));
	return *this;
}

response_head &response_head::erase_header
(const core_concepts::text_p<char> auto &name) noexcept
{
	headers.erase(name);
	return *this;
}

response_head &response_head::unset_header
(const core_concepts::text_p<char> auto &name) noexcept
{
	return erase_header(name);
}

response_head &response_head::set_cookie
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	return set_cookie ({
		.name = strtls::to_string(std::forward<decltype(name)>(name)),
		.value = std::move(value)
	});
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_RESPONSE_HEAD_H
