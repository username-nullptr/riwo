// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_FSTREAM_CONCEPTS_H
#define RIWO_CORE_UTILS_FSTREAM_CONCEPTS_H

#include <riwo/core/cxx/string_concepts.h>
#include <fstream>

namespace riwo
{

template <typename, concepts::character>
struct is_fstream : std::false_type {};

template <concepts::character CharT>
struct is_fstream<std::basic_fstream<CharT>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_fstream_v = is_fstream<T,CharT>::value;

template <typename T>
struct is_any_fstream : std::disjunction <
	is_fstream<T,char>, is_fstream<T,wchar_t>,
	is_fstream<T,char8_t>, is_fstream<T,char16_t>, is_fstream<T,char32_t>
> {};

template <typename T>
constexpr bool is_any_fstream_v = is_any_fstream<T>::value;

template <typename, concepts::character>
struct is_ofstream : std::false_type {};

template <concepts::character CharT>
struct is_ofstream<std::basic_ofstream<CharT>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_ofstream_v = is_ofstream<T,CharT>::value;

template <typename T>
struct is_any_ofstream : std::disjunction <
	is_ofstream<T,char>, is_ofstream<T,wchar_t>,
	is_ofstream<T,char8_t>, is_ofstream<T,char16_t>, is_ofstream<T,char32_t>
> {};

template <typename T>
constexpr bool is_any_ofstream_v = is_any_ofstream<T>::value;

template <typename, concepts::character>
struct is_ifstream : std::false_type {};

template <concepts::character CharT>
struct is_ifstream<std::basic_ifstream<CharT>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_ifstream_v = is_ifstream<T,CharT>::value;

template <typename T>
struct is_any_ifstream : std::disjunction <
	is_ifstream<T,char>, is_ifstream<T,wchar_t>,
	is_ifstream<T,char8_t>, is_ifstream<T,char16_t>, is_ifstream<T,char32_t>
> {};

template <typename T>
constexpr bool is_any_ifstream_v = is_any_ifstream<T>::value;

namespace concepts
{

template <typename T, typename CharT>
concept fstream =
	is_fstream_v<T,CharT> or
	is_ofstream_v<T,CharT> or
	is_ifstream_v<T,CharT>;

template <typename T, typename CharT>
concept fstream_p = fstream<std::remove_reference_t<T>,CharT>;

template <typename T>
concept any_fstream =
	fstream<T,char> or fstream<T,wchar_t> or
	fstream<T,char8_t> or fstream<T,char16_t> or fstream<T,char32_t>;

template <typename T>
concept any_fstream_p = any_fstream<std::remove_reference_t<T>>;

}} //namespace riwo


#endif //RIWO_CORE_UTILS_FSTREAM_CONCEPTS_H
