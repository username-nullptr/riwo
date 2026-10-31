// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_SYNTAX_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_SYNTAX_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http::detail
{

[[nodiscard]] RIWO_DECL_HIDDEN
std::string_view trim_ows(std::string_view value) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
bool ascii_token_character(unsigned char value) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
bool comma_list_contains(std::string_view value, std::string_view token) noexcept;

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_SYNTAX_H
