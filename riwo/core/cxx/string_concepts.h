// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_STRING_CONCEPTS_H
#define RIWO_CORE_CXX_STRING_CONCEPTS_H

#include <riwo/core/cxx/concepts.h>
#include <string>

namespace riwo
{

template <typename T>
using is_char = std::is_same<T, char>;

template <typename T>
constexpr bool is_char_v = is_char<T>::value;

template <typename T>
using is_wchar = std::is_same<T,wchar_t>;

template <typename T>
constexpr bool is_wchar_v = is_wchar<T>::value;

template <typename T>
using is_char8 = std::is_same<T, char8_t>;

template <typename T>
constexpr bool is_char8_v = is_char8<T>::value;

template <typename T>
using is_char16 = std::is_same<T, char16_t>;

template <typename T>
constexpr bool is_char16_v = is_char16<T>::value;

template <typename T>
using is_char32 = std::is_same<T, char32_t>;

template <typename T>
constexpr bool is_char32_v = is_char32<T>::value;

template <typename T>
struct is_any_char
{
	static constexpr bool value =
		is_char_v<T> or is_wchar_v<T> or
		is_char8_v<T> or is_char16_v<T> or is_char32_v<T>;
};

template <typename T>
constexpr bool is_any_char_v = is_any_char<T>::value;

namespace concepts
{

template <typename T>
concept character =
	is_char_v<T> or  is_wchar_v<T> or
	is_char8_v<T> or is_char16_v<T> or is_char32_v<T>;

template <typename T>
concept character_p = character<std::remove_cvref_t<T>>;

} //namespace concepts

template <concepts::character, typename>
struct is_char_array : std::false_type {};

template <concepts::character CharT, size_t N>
struct is_char_array<CharT, CharT[N]> : std::true_type {};

template <concepts::character CharT>
struct is_char_array<CharT, CharT[]> : std::true_type {};

template <concepts::character CharT, size_t N>
struct is_char_array<CharT, CharT(&)[N]> : std::true_type {};

template <concepts::character CharT>
struct is_char_array<CharT, CharT(&)[]> : std::true_type {};

template <concepts::character CharT, typename T>
constexpr bool is_char_array_v = is_char_array<CharT,T>::value;

template <typename T>
struct is_any_char_array : std::disjunction <
	is_char_array<char    , T>,
	is_char_array<wchar_t , T>,
	is_char_array<char8_t , T>,
	is_char_array<char16_t, T>,
	is_char_array<char32_t, T>
> {};

template <typename T>
constexpr bool is_any_char_array_v = is_any_char_array<T>::value;

template <typename, concepts::character>
struct is_std_string : std::false_type {};

template <concepts::character CharT, typename...Args>
struct is_std_string<std::basic_string<CharT,Args...>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_std_string_v = is_std_string<T,CharT>::value;

template <typename T>
struct is_any_std_string : std::disjunction <
	is_std_string<T, char    >,
	is_std_string<T, wchar_t >,
	is_std_string<T, char8_t >,
	is_std_string<T, char16_t>,
	is_std_string<T, char32_t>
> {};

template <typename T>
constexpr bool is_any_std_string_v = is_any_std_string<T>::value;

template <typename, concepts::character>
struct is_std_string_view : std::false_type {};

template <concepts::character CharT, typename...Args>
struct is_std_string_view<std::basic_string_view<CharT,Args...>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_std_string_view_v = is_std_string_view<T,CharT>::value;

template <typename T>
struct is_any_std_string_view : std::disjunction <
	is_std_string_view<T, char    >,
	is_std_string_view<T, wchar_t >,
	is_std_string_view<T, char8_t >,
	is_std_string_view<T, char16_t>,
	is_std_string_view<T, char32_t>
> {};

template <typename T>
constexpr bool is_any_std_string_view_v = is_any_std_string_view<T>::value;

template <typename T, concepts::character CharT>
struct is_string
{
	static constexpr bool value =
		is_std_string_v<T,CharT> or is_std_string_view_v<T,CharT> or
		std::is_same_v<T, const CharT*> or std::is_same_v<T, CharT*> or
		is_char_array_v<CharT, std::remove_const_t<T>>;
};

template <typename T, concepts::character CharT>
constexpr bool is_string_v = is_string<T,CharT>::value;

template <typename T>
struct is_any_string : std::disjunction <
	is_string<T, char    >,
	is_string<T, wchar_t >,
	is_string<T, char8_t >,
	is_string<T, char16_t>,
	is_string<T, char32_t>
> {};

template <typename T>
constexpr bool is_any_string_v = is_any_string<T>::value;

template <typename T, concepts::character CharT>
struct is_text : std::disjunction<
	std::is_same<T,CharT>, is_string<T,CharT>
> {};

template <typename T, concepts::character CharT>
constexpr bool is_text_v = is_text<T,CharT>::value;

template <typename T>
struct is_any_text : std::disjunction <
	is_text<T, char    >,
	is_text<T, wchar_t >,
	is_text<T, char8_t >,
	is_text<T, char16_t>,
	is_text<T, char32_t>
> {};

template <typename T>
constexpr bool is_any_text_v = is_any_text<T>::value;

namespace concepts
{

template <typename T, typename CharT>
concept string = is_string_v<T,CharT>;

template <typename T, typename CharT>
concept string_p = string<std::remove_cvref_t<T>,CharT>;

template <typename T>
concept any_string = is_any_string_v<T>;

template <typename T>
concept any_string_p = any_string<std::remove_cvref_t<T>>;

template <typename T, typename CharT>
concept text = is_text_v<T,CharT>;

template <typename T, typename CharT>
concept text_p = text<std::remove_cvref_t<T>,CharT>;

template <typename T>
concept any_text = is_any_text_v<T>;

template <typename T>
concept any_text_p = any_text<std::remove_cvref_t<T>>;

}} //namespace riwo::concepts


#endif //RIWO_CORE_CXX_STRING_CONCEPTS_H
