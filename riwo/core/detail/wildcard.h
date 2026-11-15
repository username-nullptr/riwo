// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_WILDCARD_H
#define RIWO_CORE_DETAIL_WILDCARD_H

namespace riwo
{

constexpr int32_t wildcard_match_rank
(size_t star_count, size_t question_count, size_t str_len) noexcept
{
	if( star_count == 0 and question_count == 0 )
		return 0;

	if( str_len == std::numeric_limits<size_t>::max() )
		return std::numeric_limits<int32_t>::max();

	constexpr auto max_rank = static_cast<size_t>(std::numeric_limits<int32_t>::max());
	const auto star_scale = str_len + 1;

	if( star_count > (max_rank - 1) / star_scale )
		return std::numeric_limits<int32_t>::max();

	auto rank = static_cast<size_t>(1) + star_count * star_scale;
	const auto unmatched_characters = str_len - std::min(question_count, str_len);

	if( rank > max_rank - unmatched_characters )
		return std::numeric_limits<int32_t>::max();

	return static_cast<int32_t>(rank + unmatched_characters);
}

template <concepts::any_text_p Str, concepts::text_p<strtls::get_char_t<Str>> StrArg>
int32_t wildcard_match(const Str &rule, const StrArg &str)
{
	auto rule_view = strtls::to_view(rule);
	auto str_view = strtls::to_view(str);

	size_t rule_len = rule_view.size();
	size_t str_len = str_view.size();

	size_t rule_pos = 0;
	size_t str_pos = 0;

	size_t star_pos = std::basic_string_view<strtls::get_char_t<Str>>::npos;
	size_t star_str_pos = 0;

	while( str_pos < str_len )
	{
		if( rule_pos < rule_len and
			(rule_view[rule_pos] == 0x3F/*?*/ or
			 rule_view[rule_pos] == str_view[str_pos]) )
		{
			++rule_pos;
			++str_pos;
		}
		else if( rule_pos < rule_len and rule_view[rule_pos] == 0x2A/***/ )
		{
			star_pos = rule_pos++;
			star_str_pos = str_pos;
		}
		else if( star_pos != std::basic_string_view<strtls::get_char_t<Str>>::npos )
		{
			rule_pos = star_pos + 1;
			str_pos = ++star_str_pos;
		}
		else
			return -1;
	}
	while( rule_pos < rule_len and rule_view[rule_pos] == 0x2A/***/ )
		++rule_pos;

	if( rule_pos != rule_len )
		return -1;

	size_t star_count = 0;
	size_t question_count = 0;

	for(auto ch : rule_view)
	{
		if( ch == 0x3F/*?*/ )
			++question_count;

		else if( ch == 0x2A/***/ )
			++star_count;
	}
	return wildcard_match_rank(star_count, question_count, str_len);
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_WILDCARD_H
