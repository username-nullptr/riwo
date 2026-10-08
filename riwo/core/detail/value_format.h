// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_VALUE_SERIALIZER_H
#define RIWO_CORE_DETAIL_VALUE_SERIALIZER_H

#include <riwo/core/utils/string_tools.h>

namespace riwo
{

template <concepts::character CharT,
		  typename Traits = std::char_traits<CharT>,
		  typename Alloc = std::allocator<CharT>>
class basic_value;

template <typename, concepts::character>
struct is_value : std::false_type {};

template <concepts::character CharT, typename...Args>
struct is_value<basic_value<CharT,Args...>,CharT> : std::true_type {};

template <typename T, concepts::character CharT>
constexpr bool is_value_v = is_value<T,CharT>::value;

template <typename T>
struct is_any_value : std::disjunction <
	is_value<T,char>, is_value<T,wchar_t>,
	is_value<T,char8_t>, is_value<T,char16_t>, is_value<T,char32_t>
> {};

template <typename T>
constexpr bool is_any_value_v = is_any_value<T>::value;

namespace concepts
{

template <typename T, typename CharT>
concept value = is_value_v<T,CharT>;

template <typename T, typename CharT>
concept value_p = is_value_v<std::remove_cvref_t<T>,CharT>;

template <typename T>
concept any_value = is_any_value_v<T>;

template <typename T>
concept any_value_p = is_any_value_v<std::remove_cvref_t<T>>;

} //namespace concepts

template <typename T, concepts::character CharT = char>
class RIWO_CORE_TAPI value_default_serializer
{
public:
	constexpr std::basic_string<CharT> set(const T &data) {
		return std::format(l_str(CharT,"{}"), data);
	}
};

template <typename T, concepts::character CharT = char>
class RIWO_CORE_TAPI value_serializer : public value_default_serializer<T,CharT> {};

template <concepts::integral T, concepts::character CharT>
class RIWO_CORE_TAPI value_serializer<T,CharT> : public value_default_serializer<T,CharT>
{
public:
	constexpr optional<T> get(const basic_value<CharT> &value, size_t base = 10) noexcept {
		return strtls::to_arith<T>(*value, base);
	}
};

template <concepts::floating T, concepts::character CharT>
class RIWO_CORE_TAPI value_serializer<T,CharT> : public value_default_serializer<T,CharT>
{
public:
	constexpr optional<T> get(const basic_value<CharT> &value) noexcept {
		return strtls::to_arith<T>(*value);
	}
};

template <concepts::enumerate T, concepts::character CharT>
class RIWO_CORE_TAPI value_serializer<T,CharT> : public value_default_serializer<T,CharT>
{
public:
	constexpr optional<T> get(const basic_value<CharT> &value, size_t base = 10) noexcept {
		return static_cast<T>(*value_serializer<int,CharT>().get(value, base).or_else());
	}
};

template <typename T, concepts::character CharT> requires concepts::string<T,CharT>
class RIWO_CORE_TAPI value_serializer<T,CharT>
{
public:
	constexpr decltype(auto) set(concepts::string_p<CharT> auto &&data) {
		return std::forward<decltype(data)>(data);
	}
	constexpr decltype(auto) get(concepts::value_p<CharT> auto &&value) noexcept
	{
		using Value = decltype(value);
		if constexpr( is_std_string_v<T,CharT> )
			return *std::forward<Value>(value);
		else
			return std::basic_string_view<CharT>(*value);
	}
};

template <concepts::character CharT>
class RIWO_CORE_TAPI value_serializer<basic_value<CharT>,CharT>
{
public:
	constexpr decltype(auto) set(concepts::value_p<CharT> auto &&value) {
		return *std::forward<decltype(value)>(value);
	}
	constexpr decltype(auto) get(concepts::value_p<CharT> auto &&value) noexcept {
		return std::forward<decltype(value)>(value);
	}
};

namespace concepts
{

template <typename T, typename CharT>
concept value_set = requires (
	value_serializer<std::remove_cvref_t<T>,CharT> serializer,
	std::basic_string<CharT> &str, const T &data) {
	str = serializer.set(data);
};

template <typename T>
concept any_value_set =
	value_set<T,char> or value_set<T,wchar_t> or
	value_set<T,char8_t> or value_set<T,char16_t> or value_set<T,char32_t>;

template <typename T, typename CharT, typename...Args>
concept value_get = requires (
	value_serializer<std::remove_cvref_t<T>,CharT> serializer,
	const basic_value<CharT> &value, Args&&...args) {
	serializer.get(value, std::forward<Args>(args)...);
};

template <typename T, typename...Args>
concept any_value_get =
	value_get<T,char,Args...> or value_get<T,wchar_t,Args...> or
	value_get<T,char8_t,Args...> or value_get<T,char16_t,Args...> or value_get<T,char32_t,Args...>;

}} //namespace riwo::concepts


#endif //RIWO_CORE_DETAIL_VALUE_SERIALIZER_H
