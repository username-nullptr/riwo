// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_REQUEST_HEAD_H
#define RIWO_EXP_HTTP_PROTOCOL_REQUEST_HEAD_H

#include <riwo/experimental/http/protocol/method.h>
#include <riwo/experimental/http/protocol/fields.h>
#include <riwo/experimental/http/protocol/cookie.h>
#include <riwo/core/url.h>

namespace riwo::experimental::http
{

class RIWO_EXP_HTTP_API request_head
{
public:
	experimental::http::method method {
		experimental::http::method::get
	};
	riwo::url url {};

	std::string scheme {};
	std::string authority {};
	std::string target {"/"};
	experimental::http::fields headers {};

	optional<std::string> connect_protocol {};
	optional<std::string> upgrade_protocol {};

public:
	[[nodiscard]] optional<std::string_view> header (
		const core_concepts::text_p<char> auto &name
	) const noexcept;

	[[nodiscard]] bool has_header (
		const core_concepts::text_p<char> auto &name
	) const noexcept;

	[[nodiscard]] bool contains_header (
		const core_concepts::text_p<char> auto &name
	) const noexcept;

	request_head &set_header (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);
	request_head &add_header (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);

	request_head &erase_header (
		const core_concepts::text_p<char> auto &name
	) noexcept;

	request_head &unset_header (
		const core_concepts::text_p<char> auto &name
	) noexcept;

	request_head &add_cookie (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);
	request_head &set_cookies(std::span<const cookie_pair> values);
	request_head &add_cookie(cookie_pair value);

public:
	[[nodiscard]] optional<std::string_view> content_type() const noexcept;
	[[nodiscard]] optional<uint64_t> content_length() const noexcept;

	[[nodiscard]] std::string_view host() const noexcept;
	[[nodiscard]] std::string_view path() const noexcept;

	[[nodiscard]] bool is_http() const noexcept;
	[[nodiscard]] bool is_https() const noexcept;
	[[nodiscard]] bool is_upgrade() const noexcept;
};

} //namespace riwo::experimental::http
#include <riwo/experimental/http/protocol/detail/request_head.h>


#endif //RIWO_EXP_HTTP_PROTOCOL_REQUEST_HEAD_H
