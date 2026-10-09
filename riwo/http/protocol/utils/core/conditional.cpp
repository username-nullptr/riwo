// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "conditional.h"

namespace riwo::http { namespace
{

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool valid_etag_char(unsigned char ch) noexcept
{
	return ch == 0x21 or (ch >= 0x23 and ch <= 0x7E) or ch >= 0x80;
}

[[nodiscard]] std::optional<std::string>
field_value(const headers &values, const char *name) noexcept
{
	auto it = values.find(name);
	if( it == values.end() )
		return std::nullopt;
	return it->second.to_string();
}

template <typename Func>
[[nodiscard]] bool any_entity_tag(std::string_view value, Func &&func) noexcept
{
	value = trim_ows(value);
	if( value == "*" )
		return func(value);

	size_t begin = 0;
	bool quoted = false;
	for(size_t i=0; i<=value.size(); i++)
	{
		if( i < value.size() and value[i] == '"' )
			quoted = not quoted;
		if( i != value.size() and (quoted or value[i] != ',') )
			continue;

		if( auto item = trim_ows(value.substr(begin, i - begin));
			not item.empty() and func(item) )
			return true;
		begin = i + 1;
	}
	return false;
}

[[nodiscard]] std::time_t utc_time(std::tm *value) noexcept
{
#if defined(_WIN32)
	return _mkgmtime(value);
#else //_WIN32
	return timegm(value);
#endif //_WIN32
}

[[nodiscard]] bool parse_date_format
(std::string_view input, const char *format, std::tm &result) noexcept
{
	std::istringstream stream {std::string(input)};
	stream.imbue(std::locale::classic());
	stream >> std::get_time(&result, format);
	return not stream.fail() and stream.peek() == std::char_traits<char>::eof();
}

} //namespace

optional<entity_tag> parse_entity_tag(std::string_view value) noexcept
{
	value = trim_ows(value);
	entity_tag result {};

	if( value.starts_with("W/") )
	{
		result.weak = true;
		value.remove_prefix(2);
	}
	if( value.size() < 2 or value.front() != '"' or value.back() != '"' )
		return nullopt;

	value.remove_prefix(1);
	value.remove_suffix(1);

	for(auto ch : value)
	{
		if( not valid_etag_char(static_cast<unsigned char>(ch)) )
			return nullopt;
	}
	result.opaque = value;
	return result;
}

bool strong_entity_tag_equal(std::string_view lhs, std::string_view rhs) noexcept
{
	auto left = parse_entity_tag(lhs);
	auto right = parse_entity_tag(rhs);

	return left and right and not left->weak and not right->weak and
		   left->opaque == right->opaque;
}

bool weak_entity_tag_equal(std::string_view lhs, std::string_view rhs) noexcept
{
	auto left = parse_entity_tag(lhs);
	auto right = parse_entity_tag(rhs);
	return left and right and left->opaque == right->opaque;
}

optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view value) noexcept
{
	value = trim_ows(value);
	std::tm result {};

	bool parsed = parse_date_format (
		value, "%a, %d %b %Y %H:%M:%S GMT", result
	);
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format (
			value, "%A, %d-%b-%y %H:%M:%S GMT", result
		);
		if( parsed and result.tm_year < 70 )
			result.tm_year += 100;
	}
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format (
			value, "%a %b %d %H:%M:%S %Y", result
		);
	}
	if( not parsed )
		return nullopt;

	auto seconds = utc_time(&result);
	if( seconds == static_cast<std::time_t>(-1) )
		return nullopt;
	return std::chrono::system_clock::from_time_t(seconds);
}

std::string format_http_date(std::chrono::system_clock::time_point value) noexcept
{
	static constexpr std::array<std::string_view,7> weekdays {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static constexpr std::array<std::string_view,12> months {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	std::time_t seconds = std::chrono::system_clock::to_time_t(value);
	std::tm utc {};

#if defined(_WIN32)
	gmtime_s(&utc, &seconds);
#else
	gmtime_r(&seconds, &utc);
#endif
	if( utc.tm_wday < 0 or utc.tm_wday >= static_cast<int>(weekdays.size()) or
		utc.tm_mon < 0 or utc.tm_mon >= static_cast<int>(months.size()) )
		return {};

	const auto weekday = weekdays[static_cast<size_t>(utc.tm_wday)];
	const auto month = months[static_cast<size_t>(utc.tm_mon)];

	std::array<char,64> output {};
	auto size = std::snprintf (
		output.data(), output.size(),
		"%.*s, %02d %.*s %04d %02d:%02d:%02d GMT",
		static_cast<int>(weekday.size()),
		weekday.data(),
		utc.tm_mday,
		static_cast<int>(month.size()),
		month.data(),
		utc.tm_year + 1900,
		utc.tm_hour,
		utc.tm_min,
		utc.tm_sec
	);
	if( size < 0 or static_cast<size_t>(size) >= output.size() )
		return {};
	return output.data();
}

precondition_result evaluate_preconditions(method_enum method, const headers &request_headers,
	const headers &representation_headers, bool representation_exists) noexcept
{
	auto current_etag = field_value(representation_headers, header::etag);
	auto last_modified = field_value(representation_headers, header::last_modified);

	if( auto condition = field_value(request_headers, header::if_match); condition )
	{
		bool matched = any_entity_tag(*condition, [&](std::string_view candidate)
		{
			if( candidate == "*" )
				return representation_exists;
			return representation_exists and current_etag and
				strong_entity_tag_equal(candidate, *current_etag);
		});
		if( not matched )
			return precondition_result::precondition_failed;
	}
	else if( condition = field_value(request_headers, header::if_unmodified_since);
			 condition and representation_exists and last_modified )
	{
		auto condition_date = parse_http_date(*condition);

		if( auto modified_date = parse_http_date(*last_modified);
			condition_date and modified_date and
			std::chrono::floor<std::chrono::seconds>(*modified_date) >
			std::chrono::floor<std::chrono::seconds>(*condition_date) )
			return precondition_result::precondition_failed;
	}
	if( auto condition = field_value(request_headers, header::if_none_match); condition )
	{
		bool matched = any_entity_tag(*condition, [&](std::string_view candidate) {
			if( candidate == "*" )
				return representation_exists;
			return representation_exists and current_etag and
				weak_entity_tag_equal(candidate, *current_etag);
		});
		if( matched )
		{
			return method == method::get or method == method::head ?
				precondition_result::not_modified :
				precondition_result::precondition_failed;
		}
	}
	else if( (method == method::get or method == method::head) and
			 field_value(request_headers, header::if_modified_since) )
	{
		condition = field_value(request_headers, header::if_modified_since);
		if( condition and representation_exists and last_modified )
		{
			auto condition_date = parse_http_date(*condition);

			if( auto modified_date = parse_http_date(*last_modified);
				condition_date and modified_date and
				std::chrono::floor<std::chrono::seconds>(*modified_date) <=
				std::chrono::floor<std::chrono::seconds>(*condition_date) )
				return precondition_result::not_modified;
		}
	}
	return precondition_result::proceed;
}

} //namespace riwo::http
