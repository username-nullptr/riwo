// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_STRING_TOOLS_H
#define RIWO_CORE_UTILS_DETAIL_STRING_TOOLS_H

#include <algorithm>
#include <utility>
#include <ranges>

namespace riwo::strtls { namespace detail
{

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI std::basic_string<CharT> ascii_transition(const concepts::any_text auto &text)
{
	using Text = decltype(text);
	using str_t = std::remove_cvref_t<Text>;

	if constexpr( is_any_char_v<str_t> )
		return std::basic_string<CharT>(&text,1);

	else if constexpr( std::is_same_v<get_char_t<Text>, CharT> )
	{
		if constexpr( std::is_same_v<str_t, std::basic_string<CharT>> )
			return text;
		else
			return to_string(text);
	}
	else
	{
		decltype(auto) view = to_view(std::forward<Text>(text));
		std::basic_string<CharT> res(view.size(), '\0');

		for(size_t i=0; i<view.size(); i++)
			res[i] = static_cast<CharT>(view[i]);
		return res;
	}
}

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI auto _sto_float(auto &&func, std::basic_string_view<CharT> str)
{
	size_t index = 0;
	auto res = func({str.data(), str.size()}, &index);

	using result_t = decltype(res);
	optional<result_t> opt;

	if( index >= str.size() )
		opt = res;
	return opt;
}

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI auto _sto_int(auto &&func, std::basic_string_view<CharT> str, size_t base)
{
	size_t index = 0;
	auto res = func({str.data(), str.size()}, &index, static_cast<int>(base));

	using result_t = decltype(res);
	if( index >= str.size() )
		return optional<result_t>(res);

	auto opt = _sto_float<CharT>(
		static_cast<long double(*)(const std::basic_string<CharT>&,size_t*)>(std::stold), str
	);
	return opt.transform([](auto value) {
		return static_cast<result_t>(value);
	});
}

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI int _to_bool(std::basic_string_view<CharT> str)
{
	constexpr auto true_text = l_str(CharT, "true");
	constexpr auto false_text = l_str(CharT, "false");

#ifdef WIN32
	if( _stricmp(str.data(), true_text) == 0 )
		return 1;
	else if( _stricmp(str.data(), false_text) == 0 )
		return 0;
#else
	if( str.size() == 4 and strncasecmp(str.data(), true_text, 4) == 0 )
		return 1;
	else if( str.size() == 5 and strncasecmp(str.data(), false_text, 5) == 0 )
		return 0;
#endif
	return -1;
}

template <concepts::character CharT, typename T>
[[nodiscard]] RIWO_CORE_TAPI optional<T> try_to_booltot(std::basic_string_view<CharT> str)
{
	int res = _to_bool<CharT>(str);
	if( res < 0 )
		return {};
	return static_cast<T>(!!res);
}

template <std::integral To, std::integral From>
[[nodiscard]] RIWO_CORE_TAPI optional<To>
checked_integral_cast(const optional<From> &value) noexcept
{
	if( not value or not std::in_range<To>(*value) )
		return {};
	return static_cast<To>(*value);
}

template <concepts::character CharT>
RIWO_CORE_TAPI size_t replace
(std::basic_string<CharT> &str, std::basic_string_view<CharT> find, std::basic_string_view<CharT> repl, bool step)
{
	if( find == repl )
		return 0;

	size_t sum = 0;
	size_t find_pos = 0;
	for(;;)
	{
		auto start = str.find(find, find_pos);
		if( start == std::basic_string<CharT>::npos )
			break;

		str.replace(start, find.size(), repl);
		find_pos = start;

		if( step )
			find_pos += repl.size();
		sum++;
	}
	return sum;
}

template <concepts::character CharT>
[[nodiscard]] RIWO_CORE_TAPI auto find_separator(std::basic_string_view<CharT> view)
{
	size_t pos = view.rfind(static_cast<CharT>('/'));
	return pos == std::basic_string<CharT>::npos ?
		view.rfind(static_cast<CharT>('\\')) : pos;
}

} //namespace detail

template <concepts::character CharT>
std::basic_string<CharT> to_string(concepts::integral_p auto &&value, size_t base, bool uppercase)
{
	if( base < 2 or base > 36 )
	{
		invalid_argument::loc_throw (
			"riwo::strtls::to_string: Invalid base - must be between 2 and 36"
		);
	}
	using T = std::remove_cvref_t<decltype(value)>;
	using unsigned_t = std::make_unsigned_t<decltype(+value)>;
	const bool is_negative = std::is_signed_v<T> and value < 0;
	const unsigned_t magnitude = is_negative ?
		unsigned_t {0} - static_cast<unsigned_t>(value) : static_cast<unsigned_t>(value);

	std::basic_string<CharT> result;
	if( magnitude == 0 )
		return {static_cast<CharT>('0')};

	constexpr auto digits_lower = l_str(CharT,"0123456789abcdefghijklmnopqrstuvwxyz");
	constexpr auto digits_upper = l_str(CharT,"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	const CharT *digits = uppercase ? digits_upper : digits_lower;

	auto remaining = magnitude;
	while( remaining > 0 )
	{
		auto remainder = static_cast<unsigned int>(remaining % base);
		result.insert(result.begin(), digits[remainder]);
		remaining = remaining / static_cast<unsigned_t>(base);
	}
	if( is_negative )
		result.insert(result.begin(), static_cast<CharT>('-'));
	return result;
}

template <concepts::character CharT>
std::basic_string<CharT> to_string(concepts::floating_p auto &&value) noexcept
{
	std::basic_ostringstream<CharT> oss;
	oss << value;
	return oss.str();
}

decltype(auto) to_string(concepts::any_text_p auto &&text) noexcept
{
	using Text = decltype(text);
	using text_t = std::remove_cvref_t<Text>;
	using char_t = get_char_t<Text>;

	if constexpr( is_any_std_string_v<text_t> )
		return std::forward<Text>(text);
	else if constexpr( is_any_char_v<text_t> )
		return std::basic_string<char_t>(&text,1);
	else
		return std::basic_string<char_t>(std::forward<Text>(text));
}

decltype(auto) to_view(concepts::any_text_p auto &&text) noexcept
{
	using Text = decltype(text);
	using text_t = std::remove_cvref_t<Text>;
	using char_t = get_char_t<Text>;

	if constexpr( is_any_char_v<text_t> )
		return std::basic_string_view<char_t>(&text,1);
	else if constexpr( is_any_std_string_view_v<text_t> )
		return std::forward<Text>(text);
	else
		return std::basic_string_view<char_t>(text);
}

bool is_alpha(const concepts::any_string_p auto &str) noexcept
{
	auto _str = detail::ascii_transition<char>(str);

	using Str = decltype(str);
	if constexpr( concepts::character<Str> )
		return std::isalpha(str);
	else
	{
		using char_t = get_char_t<Str>;
		using string_view_t = std::basic_string_view<char_t>;

#if RIWO_STD_CXX < 23
		string_view_t view(str);
		return std::all_of(view.begin(), view.end(), [](auto c){
			return std::isalpha(c);
		});
#else
		return std::ranges::all_of(string_view_t(str), [](auto c){
			return std::isalpha(c);
		});
#endif
	}
}

bool is_digit(const concepts::any_string_p auto &str) noexcept
{
	using Str = decltype(str);
	if constexpr( concepts::character<Str> )
		return std::isdigit(str);
	else
	{
		using char_t = get_char_t<Str>;
		using string_view_t = std::basic_string_view<char_t>;

#if RIWO_STD_CXX < 23
		string_view_t view(str);
		return std::all_of(view.begin(), view.end(), [](auto c){
			return std::isdigit(c);
		});
#else
		return std::ranges::all_of(string_view_t(str), [](auto c){
			return std::isdigit(c);
		});
#endif
	}
}

bool is_rlnum(const concepts::any_string_p auto &str) noexcept
{
	using Str = decltype(str);
	if constexpr( concepts::character<Str> )
		return std::isdigit(str);
	else
	{
		using char_t = get_char_t<Str>;
		using string_view_t = std::basic_string_view<char_t>;

		string_view_t view(str);
		if( view.empty() )
			return false;

		auto it = view.begin();
		if( *it == 0x2D/*-*/ or *it == 0x2B/*+*/ )
			++it;
		if( it == view.end() )
			return false;

		if( not std::isdigit(*it) )
			return false;

		bool dot = false;
		for(++it; it!=view.end(); ++it)
		{
			if( not std::isdigit(*it) )
			{
				if( *it == 0x2E/*.*/ and not dot )
					dot = true;
				else
					return false;
			}
		}
		return true;
	}
}

bool is_alnum(const concepts::any_string_p auto &str) noexcept
{
	using Str = decltype(str);
	if constexpr( concepts::character<Str> )
		return std::isalnum(str);
	else
	{
		using char_t = get_char_t<Str>;
		using string_view_t = std::basic_string_view<char_t>;

#if RIWO_STD_CXX < 23
		string_view_t view(str);
		return std::all_of(view.begin(), view.end(), [](auto c){
			return std::isalnum(c);
		});
#else
		return std::ranges::all_of(string_view_t(str), [](auto c){
			return std::isalnum(c);
		});
#endif
	}
}

bool is_ascii(const concepts::any_string_p auto &str) noexcept
{
	using Str = decltype(str);
	if constexpr( concepts::character<Str> )
		return str <= 0x7F;
	else
	{
		using char_t = get_char_t<Str>;
		using string_view_t = std::basic_string_view<char_t>;

#if RIWO_STD_CXX < 23
		string_view_t view(str);
		return std::all_of(view.begin(), view.end(), [](auto c){
			return c <= 0x7F;
		});
#else
		return std::ranges::all_of(string_view_t(str), [](auto c){
			return c <= 0x7F;
		});
#endif
	}
}

optional<int8_t> to_int8(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<long(*)(const string_t&,size_t*,int)>(std::stol),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<int8_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,int8_t>(_text);
}

optional<uint8_t> to_uint8(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<unsigned long(*)(const string_t&,size_t*,int)>(std::stoul),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<uint8_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,uint8_t>(_text);
}

optional<int16_t> to_int16(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<long(*)(const string_t&,size_t*,int)>(std::stol),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<int16_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,int16_t>(_text);
}

optional<uint16_t> to_uint16(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<unsigned long(*)(const string_t&,size_t*,int)>(std::stoul),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<uint16_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,uint16_t>(_text);
}

optional<int32_t> to_int32(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<long(*)(const string_t&,size_t*,int)>(std::stol),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<int32_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,int32_t>(_text);
}

optional<uint32_t> to_uint32(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<unsigned long(*)(const string_t&,size_t*,int)>(std::stoul),
			_text, base
		);
		if( auto result = detail::checked_integral_cast<uint32_t>(opt) )
			return result;
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,uint32_t>(_text);
}

optional<int64_t> to_int64(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<long long(*)(const string_t&,size_t*,int)>(std::stoll),
			_text, base
		);
		if( opt )
			return static_cast<int64_t>(*opt);
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,int64_t>(_text);
}

optional<uint64_t> to_uint64(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		auto opt = detail::_sto_int<char_t>(
			static_cast<unsigned long long(*)(const string_t&,size_t*,int)>(std::stoull),
			_text, base
		);
		if( opt )
			return static_cast<uint64_t>(*opt);
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,uint64_t>(_text);
}

optional<float> to_float(const concepts::any_text_p auto &text) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		return detail::_sto_float<char_t>(
			static_cast<float(*)(const string_t&,size_t*)>(std::stof), _text
		);
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,float>(_text);
}

optional<double> to_double(const concepts::any_text_p auto &text) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		return detail::_sto_float<char_t>(
			static_cast<double(*)(const string_t&,size_t*)>(std::stod), _text
		);
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,double>(_text);
}

optional<long double> to_ldouble(const concepts::any_text_p auto &text) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		return detail::_sto_float<char_t>(
			static_cast<long double(*)(const string_t&,size_t*)>(std::stold), _text
		);
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,long double>(_text);
}

optional<bool> to_bool(const concepts::any_text_p auto &text, size_t base) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	if constexpr( concepts::character<text_t> )
		return text != 0 and text != 0x30;
	else
	{
		using char_t = get_char_t<text_t>;
		auto _text = trimmed(text);

		int res = detail::_to_bool<char_t>(_text);
		if( res < 0 )
		{
			using string_t = std::basic_string<char_t>;
			try {
				auto value = detail::_sto_int<char_t>(
					static_cast<long(*)(const string_t&,size_t*,int)>(std::stol), _text, base
				);
				if( value )
					return *value != 0;
				return {};
			}
			catch(...) {
				return {};
			}
		}
		return res > 0;
	}
}

template <typename T>
[[nodiscard]] optional<T> to_arith(const concepts::any_text_p auto &text, size_t base)
	noexcept requires concepts::integral_p<T> or concepts::enumerate_p<T>
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	auto _text = trimmed(text);

	if constexpr( std::is_same_v<T, bool> )
		return to_bool(_text, base);

	else if constexpr( concepts::enumerate_p<T> )
	{
		return to_arith<int>(_text, base).transform([](int value) {
			return static_cast<T>(value);
		});
	}
	else
	{
		using string_t = std::basic_string<char_t>;
		try {
			if constexpr( std::is_signed_v<T> and sizeof(T) <= sizeof(long) )
				return detail::checked_integral_cast<T>(detail::_sto_int<char_t>(
					static_cast<long(*)(const string_t&,size_t*,int)>(std::stol), _text, base));
			else if constexpr( std::is_signed_v<T> )
				return detail::checked_integral_cast<T>(detail::_sto_int<char_t>(
					static_cast<long long(*)(const string_t&,size_t*,int)>(std::stoll), _text, base));
			else if constexpr( sizeof(T) <= sizeof(unsigned long) )
				return detail::checked_integral_cast<T>(detail::_sto_int<char_t>(
					static_cast<unsigned long(*)(const string_t&,size_t*,int)>(std::stoul), _text, base));
			else
				return detail::checked_integral_cast<T>(detail::_sto_int<char_t>(
					static_cast<unsigned long long(*)(const string_t&,size_t*,int)>(std::stoull), _text, base));
		}
		catch(std::exception&) {}
		return detail::try_to_booltot<char_t,T>(_text);
	}
}

template <concepts::floating_p T>
[[nodiscard]] optional<T> to_arith(const concepts::any_text_p auto &text) noexcept
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;
	using string_t = std::basic_string<char_t>;

	auto _text = trimmed(text);
	try {
		if constexpr( std::is_same_v<T, float> )
		{
			return detail::_sto_float<char_t>(
				static_cast<float(*)(const string_t&,size_t*)>(std::stof), _text
			);
		}
		else if constexpr( std::is_same_v<T, double> )
		{
			return detail::_sto_float<char_t>(
				static_cast<double(*)(const string_t&,size_t*)>(std::stod), _text
			);
		}
		else if constexpr( std::is_same_v<T, long double> )
		{
			return detail::_sto_float<char_t>(
				static_cast<long double(*)(const string_t&,size_t*)>(std::stold), _text
			);
		}
	}
	catch(std::exception&) {}
	return detail::try_to_booltot<char_t,T>(_text);
}

auto to_lower(concepts::any_text_p auto &&text)
{
	using Text = decltype(text);
	using text_t = std::remove_cvref_t<Text>;
	using char_t = get_char_t<text_t>;

	if constexpr( concepts::character<text_t> )
		return static_cast<char_t>(std::tolower(text));
	else
	{
		std::basic_string<char_t> result(std::forward<Text>(text));
		for(auto &c : result)
			c = static_cast<char_t>(std::tolower(c));
		return result;
	}
}

auto to_upper(concepts::any_text_p auto &&text)
{
	using Text = decltype(text);
	using text_t = std::remove_cvref_t<Text>;
	using char_t = get_char_t<text_t>;

	if constexpr( concepts::character<text_t> )
		return static_cast<char_t>(std::toupper(text));
	else
	{
		std::basic_string<char_t> result(std::forward<Text>(text));
		for(auto &c : result)
			c = static_cast<char_t>(std::toupper(c));
		return result;
	}
}

template <concepts::any_string_p Str>
auto replace(Str &&str, concepts::text_p<get_char_t<Str>> auto &&find,
			 concepts::text_p<get_char_t<Str>> auto &&repl, bool step)
{
	std::basic_string<get_char_t<Str>> result(std::forward<Str>(str));
	detail::replace(result,
		to_view(std::forward<decltype(find)>(find)),
		to_view(std::forward<decltype(repl)>(repl)),
		step
	);
	return result;
}

template <concepts::any_string_p Str>
auto replace(size_t &count, Str &&str, concepts::text_p<get_char_t<Str>> auto &&find,
			 concepts::text_p<get_char_t<Str>> auto &&repl, bool step)
{
	std::basic_string<get_char_t<Str>> result(std::forward<Str>(str));
	count = detail::replace(result,
		to_view(std::forward<decltype(find)>(find)),
		to_view(std::forward<decltype(repl)>(repl)),
		step
	);
	return result;
}

auto trimmed(const concepts::any_text_p auto &text)
{
	using text_t = std::remove_cvref_t<decltype(text)>;
	using char_t = get_char_t<text_t>;

	if constexpr( concepts::character<text_t> )
		return trimmed(std::basic_string_view(&text,1));
	else
	{
		decltype(auto) view = to_view(text);
		std::basic_string<char_t> result;

		size_t left = 0;
		while( left < view.size() )
		{
			if( view[left] >= 1 and view[left] <= 32 )
				left++;
			else
				break;
		}
		if( left >= view.size() )
			return result;

		size_t right = view.size();
		while( right > left )
		{
			if( view[right - 1] >= 1 and view[right - 1] <= 32 )
				--right;
			else
				break;
		}
		if( right == left )
			return result;

		result.assign(view.substr(left, right - left));
		return result;
	}
}

template <concepts::any_string_p Str, concepts::text_p<get_char_t<Str>> Find>
auto remove(const Str &str, const Find &find, bool step)
{
	using str_t = std::remove_cvref_t<decltype(str)>;
	using char_t = get_char_t<str_t>;

	if constexpr( concepts::character<str_t> )
	{
		std::basic_string<char_t> res(str.data(), str.size());
		auto it = std::remove(res.begin(), res.end(), find);
		if( it != res.end() )
			res.erase(it, res.end());
		return res;
	}
	else
	{
		std::basic_string<char_t> res(str.data(), str.size());
		res = replace(std::move(res), find, std::basic_string<char_t>(), step);
		return res;
	}
}

auto file_name(const concepts::any_text_p auto &file_name)
{
	using text_t = std::remove_cvref_t<decltype(file_name)>;
	using char_t = get_char_t<text_t>;

	using str_t = std::basic_string<char_t>;
	using str_view_t = std::basic_string_view<char_t>;

	if constexpr( concepts::character<text_t> )
		return str_t(&file_name,1);
	else
	{
		decltype(auto) view = to_view(file_name);
		auto pos = detail::find_separator(view);

		if( pos == str_view_t::npos )
			return str_t(view);

		auto tmp = view.substr(pos + 1);
		return str_t(tmp);
	}
}

auto file_path(const concepts::any_text_p auto &file_name)
{
	using text_t = std::remove_cvref_t<decltype(file_name)>;
	using char_t = get_char_t<text_t>;

	using str_t = std::basic_string<char_t>;
	using str_view_t = std::basic_string_view<char_t>;

	if constexpr( concepts::character<text_t> )
		return strtls::file_path(str_view_t(&file_name,1));
	else
	{
		decltype(auto) view = to_view(file_name);
		auto pos = detail::find_separator(view);

		if( pos == str_view_t::npos )
			return str_t(l_str(char_t,"./"));

		auto tmp = view.substr(0, pos + 1);
		return str_t(tmp);
	}
}

} //namespace riwo::strtls


#endif //RIWO_CORE_UTILS_DETAIL_STRING_TOOLS_H
