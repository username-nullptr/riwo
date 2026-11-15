// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_WILDCARD_H
#define RIWO_CORE_WILDCARD_H

#include <riwo/core/string_tools.h>

namespace riwo
{

// Lower ranks are more specific. Any number of constrained characters is
// preferred over an unconstrained wildcard; among rules with the same star
// count, each additional '?' increases specificity.
[[nodiscard]] constexpr int32_t wildcard_match_rank (
	size_t star_count, size_t question_count, size_t str_len
) noexcept;

template <concepts::any_text_p Str, concepts::text_p<strtls::get_char_t<Str>> StrArg>
[[nodiscard]] RIWO_CORE_TAPI int32_t wildcard_match (
	const Str &rule, const StrArg &str
);

} //namespace riwo
#include <riwo/core/detail/wildcard.h>


#endif //RIWO_CORE_WILDCARD_H
