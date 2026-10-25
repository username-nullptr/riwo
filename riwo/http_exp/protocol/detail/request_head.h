// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_REQUEST_HEAD_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_REQUEST_HEAD_H

namespace riwo::http_exp
{

optional<std::string_view> request_head::header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return headers.find_first(name);
}

bool request_head::has_header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return headers.find_first(name).has_value();
}

bool request_head::contains_header
(const core_concepts::text_p<char> auto &name) const noexcept
{
	return has_header(name);
}

request_head &request_head::set_header
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	headers.set(std::forward<decltype(name)>(name), std::move(value));
	return *this;
}

request_head &request_head::add_header
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	headers.add(std::forward<decltype(name)>(name), std::move(value));
	return *this;
}

request_head &request_head::erase_header
(const core_concepts::text_p<char> auto &name) noexcept
{
	headers.erase(name);
	return *this;
}

request_head &request_head::unset_header
(const core_concepts::text_p<char> auto &name) noexcept
{
	return erase_header(name);
}

request_head &request_head::add_cookie
(core_concepts::text_p<char> auto &&name, riwo::value value)
{
	return add_cookie ({
		.name = strtls::to_string(std::forward<decltype(name)>(name)),
		.value = std::move(value)
	});
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_REQUEST_HEAD_H
