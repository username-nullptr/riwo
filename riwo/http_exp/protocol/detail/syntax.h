// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_SYNTAX_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_SYNTAX_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp::detail
{

[[nodiscard]] RIWO_HTTP_EXP_API
std::string_view trim_ows(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
bool ascii_token_character(unsigned char value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
bool comma_list_contains(std::string_view value, std::string_view token) noexcept;

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_SYNTAX_H
