// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_REQUEST_HEAD_H
#define RIWO_HTTP_EXP_PROTOCOL_REQUEST_HEAD_H

#include <riwo/http_exp/protocol/method.h>
#include <riwo/http_exp/protocol/fields.h>
#include <riwo/core/url.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API request_head
{
public:
	http_exp::method method {http_exp::method::get};
	riwo::url url {};
	std::string scheme {};
	std::string authority {};
	std::string target {"/"};
	http_exp::fields headers {};
	optional<std::string> connect_protocol {};
	optional<std::string> upgrade_protocol {};

public:
	[[nodiscard]] optional<std::string_view>
	header(std::string_view name) const noexcept;

	[[nodiscard]] bool has_header(std::string_view name) const noexcept;
	[[nodiscard]] bool contains_header(std::string_view name) const noexcept;

	request_head &set_header(std::string name, riwo::value value);
	request_head &add_header(std::string name, riwo::value value);
	request_head &erase_header(std::string_view name) noexcept;
	request_head &unset_header(std::string_view name) noexcept;

	[[nodiscard]] optional<std::string_view> content_type() const noexcept;
	[[nodiscard]] optional<uint64_t> content_length() const noexcept;

	[[nodiscard]] std::string_view host() const noexcept;
	[[nodiscard]] std::string_view path() const noexcept;

	[[nodiscard]] bool is_http() const noexcept;
	[[nodiscard]] bool is_https() const noexcept;
	[[nodiscard]] bool is_upgrade() const noexcept;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_REQUEST_HEAD_H
