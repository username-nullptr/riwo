// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_DETAIL_MISC_H
#define RIWO_CORE_ALGORITHM_DETAIL_MISC_H

namespace riwo { namespace detail
{

template <concepts::character CharT>
constexpr CharT to_hex_upper(unsigned int value) noexcept
{
	constexpr auto str = l_str(CharT,"0123456789ABCDEF");
	return str[value & 0xF];
}

template <concepts::character CharT>
constexpr CharT to_hex_lower(unsigned int value) noexcept
{
	constexpr auto str = l_str(CharT,"0123456789abcdef");
	return str[value & 0xF];
}

} //namespace detail

auto from_percent_encoding(concepts::any_string_p auto &&str, char percent)
{
	using Str = decltype(str);
	using char_t = strtls::get_char_t<Str>;

	std::basic_string<char_t> result(std::forward<Str>(str));
	if( result.empty() )
		return result;

	auto input_ptr = result.c_str();
	auto data = result.data();

	size_t i = 0;
	size_t outlen = 0;
	size_t len = result.size();

	int a = 0, b = 0;
	char_t c = 0;

	while( i < len )
	{
		c = input_ptr[i];
		if( c == static_cast<char_t>(percent) and i + 2 < len )
		{
			a = input_ptr[++i];
			b = input_ptr[++i];

			if( a >= 0x30/*0*/ and a <= 0x39/*9*/ )
				a -= 0x30/*0*/;

			else if( a >= 0x61/*a*/ and a <= 0x66/*f*/ )
				a = a - 0x61/*a*/ + 10;

			else if( a >= 0x41/*A*/ && a <= 0x46/*F*/ )
				a = a - 0x41/*A*/ + 10;

			if( b >= 0x30/*0*/ and b <= 0x39/*9*/ )
				b -= 0x30/*0*/;

			else if( b >= 0x61/*a*/ and b <= 0x66/*f*/ )
				b = b - 0x61/*a*/ + 10;

			else if( b >= 0x41/*A*/ and b <= 0x46/*F*/ )
				b = b - 0x41/*A*/ + 10;

			*data++ = static_cast<char_t>((a << 4) | b);
		}
		else
			*data++ = c;
		++i;
		++outlen;
	}
	if( outlen != len )
		result.resize(outlen);
	return result;
}

template <concepts::any_text_p Str, concepts::text_p<strtls::get_char_t<Str>> StrArg>
auto to_percent_encoding(const Str &str, StrArg &&exclude, StrArg &&include, char percent)
{
	using char_t = strtls::get_char_t<Str>;
	std::basic_string<char_t> result;

	decltype(auto) str_view = strtls::to_view(str);
	if( str_view.empty() )
		return result;

	decltype(auto) exclude_view = strtls::to_view(exclude);
	decltype(auto) include_view = strtls::to_view(include);

	const auto contains = [](std::basic_string_view<char_t> view, char_t c) {
		return not view.empty() and view.find(c) != std::basic_string_view<char_t>::npos;
	};
	size_t length = 0;
	result.resize(str_view.size());
	bool expanded = false;

	for(auto &c : str_view)
	{
		if( c != static_cast<char_t>(percent) and
			((c >= static_cast<char_t>(0x61) and c <= static_cast<char_t>(0x7A)) // ALPHA
			 or (c >= static_cast<char_t>(0x41) and c <= static_cast<char_t>(0x5A)) // ALPHA
			 or (c >= static_cast<char_t>(0x30) and c <= static_cast<char_t>(0x39)) // DIGIT
			 or c == static_cast<char_t>(0x2D) // -
			 or c == static_cast<char_t>(0x2E) // .
			 or c == static_cast<char_t>(0x5F) // _
			 or c == static_cast<char_t>(0x7E) // ~
			 or contains(exclude_view, c)) and
			not contains(include_view, c) )
		{
			result[length++] = c;
		}
		else
		{
			if( not expanded )
			{
				result.resize(str_view.size() * 3);
				expanded = true;
			}
			using unsigned_char_t = std::make_unsigned_t<char_t>;

			const auto code_unit = static_cast<unsigned_char_t>(c);
			result[length++] = percent;

			result[length++] = detail::to_hex_upper<char_t>(
				static_cast<unsigned int>((code_unit & 0xf0U) >> 4U)
			);
			result[length++] = detail::to_hex_upper<char_t>(
				static_cast<unsigned int>(code_unit & 0x0fU)
			);
		}
	}
	if( expanded )
		result.resize(length);
	return result;
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

	size_t unit_weight = 0;
	for(auto ch : rule_view)
	{
		if( ch == 0x3F/*?*/ )
			++unit_weight;

		else if( ch == 0x2A/***/ )
			unit_weight += 2;
	}
	if( unit_weight != 0 and
		str_len > static_cast<size_t>(std::numeric_limits<int32_t>::max()) / unit_weight )
		return std::numeric_limits<int32_t>::max();

	return static_cast<int32_t>(str_len * unit_weight);
}

} //namespace riwo


#endif //RIWO_CORE_ALGORITHM_DETAIL_MISC_H
