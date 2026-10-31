// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_UTILS_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_UTILS_H

#include <riwo/experimental/http/protocol/detail/syntax.h>
#include <riwo/core/utils/string_tools.h>
#include <random>

namespace riwo::experimental::http::detail
{

[[nodiscard]] RIWO_DECL_HIDDEN
bool valid_multipart_boundary(std::string_view value, bool quoted = false) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<std::string> parse_multipart_boundary (
	std::string_view content_type, std::string_view expected_media_type
) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
std::string make_multipart_boundary();

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_UTILS_H
