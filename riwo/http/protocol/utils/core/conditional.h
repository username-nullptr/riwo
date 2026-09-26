// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_CONDITIONAL_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_CONDITIONAL_H

#include <riwo/http/protocol/header.h>
#include <riwo/http/protocol/types.h>

namespace riwo::http
{

struct RIWO_HTTP_API entity_tag
{
	bool weak = false;
	std::string opaque {};
};

enum class precondition_result
{
	proceed,
	not_modified,
	precondition_failed
};

[[nodiscard]] RIWO_HTTP_API optional<entity_tag>
parse_entity_tag(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
strong_entity_tag_equal(std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
weak_entity_tag_equal(std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] RIWO_HTTP_API optional<std::chrono::system_clock::time_point>
parse_http_date(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_API std::string
format_http_date(std::chrono::system_clock::time_point value) noexcept;

[[nodiscard]] RIWO_HTTP_API precondition_result evaluate_preconditions (
	method_enum method, const headers &request_headers, const headers &representation_headers,
	bool representation_exists = true
) noexcept;

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_CONDITIONAL_H
