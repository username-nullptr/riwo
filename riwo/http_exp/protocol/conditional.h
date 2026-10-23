// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_CONDITIONAL_H
#define RIWO_HTTP_EXP_PROTOCOL_CONDITIONAL_H

#include <riwo/http_exp/protocol/fields.h>
#include <riwo/http_exp/protocol/method.h>

namespace riwo::http_exp
{

struct entity_tag
{
	bool weak = false;
	std::string opaque;
};

enum class precondition_result
{
	proceed,
	not_modified,
	precondition_failed
};

[[nodiscard]] RIWO_HTTP_EXP_API optional<entity_tag>
parse_entity_tag(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API bool strong_entity_tag_equal(
	std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API bool weak_entity_tag_equal(
	std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
optional<std::chrono::system_clock::time_point>
parse_http_date(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API std::string
format_http_date(std::chrono::system_clock::time_point value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API precondition_result evaluate_preconditions(
	const http_exp::method &request_method, const fields &request_headers,
	const fields &representation_headers,
	bool representation_exists = true) noexcept;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_CONDITIONAL_H
