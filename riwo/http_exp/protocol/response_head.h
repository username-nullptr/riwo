// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_RESPONSE_HEAD_H
#define RIWO_HTTP_EXP_PROTOCOL_RESPONSE_HEAD_H

#include <riwo/http_exp/protocol/status.h>
#include <riwo/http_exp/protocol/fields.h>
#include <riwo/http_exp/protocol/cookie.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API response_head
{
public:
	http_exp::status status {
		http_exp::status::ok
	};
	fields headers {};
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

	response_head &set_header (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);
	response_head &add_header (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);

	response_head &erase_header (
		const core_concepts::text_p<char> auto &name
	) noexcept;

	response_head &unset_header (
		const core_concepts::text_p<char> auto &name
	) noexcept;

	response_head &set_cookie (
		core_concepts::text_p<char> auto &&name, riwo::value value
	);
	response_head &set_cookie (
		const response_cookie &value
	);

public:
	[[nodiscard]] optional<std::string_view> content_type() const noexcept;
	[[nodiscard]] optional<uint64_t> content_length() const noexcept;
	[[nodiscard]] optional<std::string_view> location() const noexcept;

	[[nodiscard]] bool is_informational() const noexcept;
	[[nodiscard]] bool is_success() const noexcept;
	[[nodiscard]] bool is_redirection() const noexcept;

	[[nodiscard]] bool is_client_error() const noexcept;
	[[nodiscard]] bool is_server_error() const noexcept;
	[[nodiscard]] bool allows_body() const noexcept;

	[[nodiscard]] bool is_error() const noexcept;
};

} //namespace riwo::http_exp
#include <riwo/http_exp/protocol/detail/response_head.h>


#endif //RIWO_HTTP_EXP_PROTOCOL_RESPONSE_HEAD_H
