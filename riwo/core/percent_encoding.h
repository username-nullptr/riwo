// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_PERCENT_ENCODING_H
#define RIWO_CORE_PERCENT_ENCODING_H

#include <riwo/core/string_tools.h>

namespace riwo
{

template <concepts::character CharT>
constexpr CharT to_hex_lower(uint32_t val) noexcept;

template <concepts::character CharT>
constexpr CharT to_hex_upper(uint32_t val) noexcept;

[[nodiscard]] RIWO_CORE_TAPI auto from_percent_encoding (
	concepts::any_string_p auto &&str, char percent = '%'
);

template <concepts::any_text_p Str, concepts::text_p<strtls::get_char_t<Str>> StrArg =
		  std::basic_string_view<strtls::get_char_t<Str>>>
[[nodiscard]] RIWO_CORE_TAPI auto to_percent_encoding (
	const Str &str, StrArg &&exclude = {}, StrArg &&include = {}, char percent = '%'
);

} //namespace riwo
#include <riwo/core/detail/percent_encoding.h>


#endif //RIWO_CORE_PERCENT_ENCODING_H
