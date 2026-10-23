// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "conditional.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] optional<std::string_view> field_value(
	const fields &values, std::string_view name) noexcept {
	return values.find_first(name);
}

template <typename Func>
[[nodiscard]] bool any_entity_tag(std::string_view value, Func &&func) noexcept
{
	value = trim(value);
	if( value == "*" ) return func(value);
	size_t begin = 0;
	bool quoted = false;
	for( size_t index = 0; index <= value.size(); ++index )
	{
		if( index < value.size() and value[index] == '"' ) quoted = not quoted;
		if( index != value.size() and (quoted or value[index] != ',') ) continue;
		const auto item = trim(value.substr(begin, index - begin));
		if( not item.empty() and func(item) ) return true;
		begin = index + 1;
	}
	return false;
}

[[nodiscard]] std::time_t utc_time(std::tm *value) noexcept
{
#if defined(_WIN32)
	return _mkgmtime(value);
#else
	return timegm(value);
#endif
}

[[nodiscard]] bool parse_date_format(std::string_view input,
	const char *format, std::tm &result) noexcept
{
	try
	{
		std::istringstream stream {std::string(input)};
		stream.imbue(std::locale::classic());
		stream >> std::get_time(&result, format);
		return not stream.fail() and
			stream.peek() == std::char_traits<char>::eof();
	}
	catch(...) { return false; }
}

} //namespace

optional<entity_tag> parse_entity_tag(std::string_view value) noexcept
{
	value = trim(value);
	entity_tag result;
	if( value.starts_with("W/") )
	{
		result.weak = true;
		value.remove_prefix(2);
	}
	if( value.size() < 2 or value.front() != '"' or value.back() != '"' )
		return {};
	value.remove_prefix(1);
	value.remove_suffix(1);
	for( const auto ch : value )
	{
		const auto byte = static_cast<unsigned char>(ch);
		if( not (byte == 0x21 or (byte >= 0x23 and byte <= 0x7e) or
			byte >= 0x80) ) return {};
	}
	result.opaque = value;
	return result;
}

bool strong_entity_tag_equal(std::string_view lhs,
	std::string_view rhs) noexcept
{
	const auto left = parse_entity_tag(lhs);
	const auto right = parse_entity_tag(rhs);
	return left and right and not left->weak and not right->weak and
		left->opaque == right->opaque;
}

bool weak_entity_tag_equal(std::string_view lhs,
	std::string_view rhs) noexcept
{
	const auto left = parse_entity_tag(lhs);
	const auto right = parse_entity_tag(rhs);
	return left and right and left->opaque == right->opaque;
}

optional<std::chrono::system_clock::time_point> parse_http_date(
	std::string_view value) noexcept
{
	value = trim(value);
	std::tm result {};
	bool parsed = parse_date_format(value, "%a, %d %b %Y %H:%M:%S GMT", result);
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format(value, "%A, %d-%b-%y %H:%M:%S GMT", result);
		if( parsed and result.tm_year < 70 ) result.tm_year += 100;
	}
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format(value, "%a %b %d %H:%M:%S %Y", result);
	}
	if( not parsed ) return {};
	const auto seconds = utc_time(&result);
	if( seconds == static_cast<std::time_t>(-1) ) return {};
	return std::chrono::system_clock::from_time_t(seconds);
}

std::string format_http_date(
	std::chrono::system_clock::time_point value) noexcept
{
	static constexpr std::array<std::string_view,7> weekdays {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static constexpr std::array<std::string_view,12> months {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	const auto seconds = std::chrono::system_clock::to_time_t(value);
	std::tm utc {};
#if defined(_WIN32)
	gmtime_s(&utc, &seconds);
#else
	gmtime_r(&seconds, &utc);
#endif
	if( utc.tm_wday < 0 or utc.tm_wday >= 7 or utc.tm_mon < 0 or
		utc.tm_mon >= 12 ) return {};
	std::array<char,64> output {};
	const auto size = std::snprintf(output.data(), output.size(),
		"%.*s, %02d %.*s %04d %02d:%02d:%02d GMT",
		3, weekdays[static_cast<size_t>(utc.tm_wday)].data(), utc.tm_mday,
		3, months[static_cast<size_t>(utc.tm_mon)].data(), utc.tm_year + 1900,
		utc.tm_hour, utc.tm_min, utc.tm_sec);
	if( size < 0 or static_cast<size_t>(size) >= output.size() ) return {};
	return output.data();
}

precondition_result evaluate_preconditions(const http_exp::method &request_method,
	const fields &request_headers, const fields &representation_headers,
	bool representation_exists) noexcept
{
	const auto current_etag = field_value(representation_headers, "etag");
	const auto last_modified = field_value(representation_headers,
		"last-modified");
	if( const auto condition = field_value(request_headers, "if-match") )
	{
		const bool matched = any_entity_tag(*condition,
		[&](std::string_view candidate) {
			return candidate == "*" ? representation_exists :
				representation_exists and current_etag and
				strong_entity_tag_equal(candidate, *current_etag);
		});
		if( not matched ) return precondition_result::precondition_failed;
	}
	else if( const auto unmodified_since = field_value(request_headers,
		"if-unmodified-since"); unmodified_since and representation_exists and
		last_modified )
	{
		const auto requested = parse_http_date(*unmodified_since);
		const auto modified = parse_http_date(*last_modified);
		if( requested and modified and
			std::chrono::floor<std::chrono::seconds>(*modified) >
			std::chrono::floor<std::chrono::seconds>(*requested) )
			return precondition_result::precondition_failed;
	}
	if( const auto condition = field_value(request_headers, "if-none-match") )
	{
		const bool matched = any_entity_tag(*condition,
		[&](std::string_view candidate) {
			return candidate == "*" ? representation_exists :
				representation_exists and current_etag and
				weak_entity_tag_equal(candidate, *current_etag);
		});
		if( matched )
			return request_method.known() == method::get or
				request_method.known() == method::head ?
				precondition_result::not_modified :
				precondition_result::precondition_failed;
	}
	else if( (request_method.known() == method::get or
		request_method.known() == method::head) )
	{
		const auto modified_since = field_value(request_headers,
			"if-modified-since");
		if( modified_since and representation_exists and last_modified )
		{
			const auto requested = parse_http_date(*modified_since);
			const auto modified = parse_http_date(*last_modified);
			if( requested and modified and
				std::chrono::floor<std::chrono::seconds>(*modified) <=
				std::chrono::floor<std::chrono::seconds>(*requested) )
				return precondition_result::not_modified;
		}
	}
	return precondition_result::proceed;
}

} //namespace riwo::http_exp
