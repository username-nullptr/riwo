// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_PERCENT_ENCODING_H
#define RIWO_CORE_DETAIL_PERCENT_ENCODING_H

namespace riwo
{

template <concepts::character CharT>
constexpr CharT to_hex_lower(uint32_t val) noexcept
{
	constexpr auto str = l_str(CharT,"0123456789abcdef");
	return str[val & 0xF];
}

template <concepts::character CharT>
constexpr CharT to_hex_upper(uint32_t val) noexcept
{
	constexpr auto str = l_str(CharT,"0123456789ABCDEF");
	return str[val & 0xF];
}

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
	size_t out_len = 0;
	size_t len = result.size();
	char_t c = 0;

	while( i < len )
	{
		c = input_ptr[i];
		if( c == static_cast<char_t>(percent) and i + 2 < len )
		{
			int a = input_ptr[++i];
			int b = input_ptr[++i];

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
		++out_len;
	}
	if( out_len != len )
		result.resize(out_len);
	return result;
}

template <concepts::any_text_p Str, concepts::text_p<strtls::get_char_t<Str>> StrArg>
auto to_percent_encoding(const Str &str, StrArg &&exclude, StrArg &&include, char percent)
{
	using char_t = strtls::get_char_t<Str>;
	std::basic_string<char_t> result {};

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

			result[length++] = to_hex_upper<char_t>(
				static_cast<unsigned int>((code_unit & 0xf0U) >> 4U)
			);
			result[length++] = to_hex_upper<char_t>(
				static_cast<unsigned int>(code_unit & 0x0fU)
			);
		}
	}
	if( expanded )
		result.resize(length);
	return result;
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_PERCENT_ENCODING_H
