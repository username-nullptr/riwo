// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "cookie.h"
#include <riwo/core/utils/string_tools.h>
#include <riwo/http_exp/error.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] bool token_character(unsigned char value) noexcept
{
	return (value >= '0' and value <= '9') or
		(value >= 'A' and value <= 'Z') or
		(value >= 'a' and value <= 'z') or
		std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(value)) !=
		std::string_view::npos;
}

[[nodiscard]] bool valid_token(std::string_view value) noexcept
{
	return not value.empty() and
		std::ranges::all_of(value, token_character);
}

[[nodiscard]] bool cookie_octet(unsigned char value) noexcept
{
	return value == 0x21 or (value >= 0x23 and value <= 0x2B) or
		(value >= 0x2D and value <= 0x3A) or
		(value >= 0x3C and value <= 0x5B) or
		(value >= 0x5D and value <= 0x7E);
}

[[nodiscard]] bool valid_cookie_value(std::string_view value) noexcept
{
	if( value.size() >= 2 and value.front() == '"' and value.back() == '"' )
	{
		value.remove_prefix(1);
		value.remove_suffix(1);
	}
	else if( value.find('"') != std::string_view::npos )
		return false;
	return std::ranges::all_of(value, cookie_octet);
}

[[nodiscard]] bool valid_attribute_value(std::string_view value) noexcept
{
	return std::ranges::all_of(value, [](unsigned char byte) {
		return byte >= 0x20 and byte != 0x7F and byte != ';';
	});
}

[[nodiscard]] bool valid_domain(std::string_view value) noexcept
{
	if( value.starts_with('.') )
		value.remove_prefix(1);
	if( value.empty() or value.size() > 253 or value.ends_with('.') )
		return false;
	size_t begin = 0;
	while( begin < value.size() )
	{
		const auto end = value.find('.', begin);
		const auto label = value.substr(begin,
			end == std::string_view::npos ? std::string_view::npos : end - begin);
		if( label.empty() or label.size() > 63 or label.starts_with('-') or
			label.ends_with('-') or
			not std::ranges::all_of(label, [](unsigned char byte) {
				return (byte >= '0' and byte <= '9') or
					(byte >= 'A' and byte <= 'Z') or
					(byte >= 'a' and byte <= 'z') or byte == '-';
			}) )
			return false;
		if( end == std::string_view::npos )
			break;
		begin = end + 1;
	}
	return true;
}

[[nodiscard]] std::string lower(std::string_view value)
{
	return strtls::to_lower(value);
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
	try {
		std::istringstream stream {std::string(input)};
		stream.imbue(std::locale::classic());
		stream >> std::get_time(&result, format);
		return not stream.fail() and
			stream.peek() == std::char_traits<char>::eof();
	}
	catch(...) {
		return false;
	}
}

[[nodiscard]] optional<std::chrono::system_clock::time_point>
parse_http_date(std::string_view value) noexcept
{
	value = trim_ows(value);
	std::tm result {};
	bool parsed = parse_date_format(value,
		"%a, %d %b %Y %H:%M:%S GMT", result);
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format(value,
			"%A, %d-%b-%y %H:%M:%S GMT", result);
		if( parsed and result.tm_year < 70 )
			result.tm_year += 100;
	}
	if( not parsed )
	{
		result = {};
		parsed = parse_date_format(value, "%a %b %d %H:%M:%S %Y", result);
	}
	if( not parsed )
		return {};
	const auto seconds = utc_time(&result);
	if( seconds == static_cast<std::time_t>(-1) )
		return {};
	return std::chrono::system_clock::from_time_t(seconds);
}

[[nodiscard]] std::string format_http_date(
	std::chrono::system_clock::time_point value)
{
	static constexpr std::array<std::string_view,7> weekdays {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static constexpr std::array<std::string_view,12> months {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	const std::time_t seconds = std::chrono::system_clock::to_time_t(value);
	std::tm utc {};
#if defined(_WIN32)
	if( gmtime_s(&utc, &seconds) != 0 )
		throw std::invalid_argument("Invalid Cookie expiry");
#else
	if( not gmtime_r(&seconds, &utc) )
		throw std::invalid_argument("Invalid Cookie expiry");
#endif
	if( utc.tm_wday < 0 or utc.tm_wday >= static_cast<int>(weekdays.size()) or
		utc.tm_mon < 0 or utc.tm_mon >= static_cast<int>(months.size()) )
		throw std::invalid_argument("Invalid Cookie expiry");
	const auto weekday = weekdays[static_cast<size_t>(utc.tm_wday)];
	const auto month = months[static_cast<size_t>(utc.tm_mon)];
	std::array<char,64> output {};
	const auto size = std::snprintf(output.data(), output.size(),
		"%.*s, %02d %.*s %04d %02d:%02d:%02d GMT",
		static_cast<int>(weekday.size()), weekday.data(), utc.tm_mday,
		static_cast<int>(month.size()), month.data(), utc.tm_year + 1900,
		utc.tm_hour, utc.tm_min, utc.tm_sec);
	if( size <= 0 or static_cast<size_t>(size) >= output.size() )
		throw std::invalid_argument("Invalid Cookie expiry");
	return {output.data(), static_cast<size_t>(size)};
}

[[nodiscard]] optional<std::chrono::seconds>
parse_max_age(std::string_view value) noexcept
{
	value = trim_ows(value);
	if( value.empty() )
		return {};
	int64_t seconds = 0;
	const auto parsed = std::from_chars(value.data(),
		value.data() + value.size(), seconds);
	if( parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size() )
		return {};
	return std::chrono::seconds(seconds);
}

template <typename Value>
[[nodiscard]] sys_expected<Value> invalid_cookie() noexcept
{
	return sys_unexpected(make_error_code(errc::invalid_cookie));
}

[[nodiscard]] sys_expected<cookie_pair>
parse_cookie_pair(std::string_view statement) noexcept
{
	try {
		statement = trim_ows(statement);
		const auto separator = statement.find('=');
		if( separator == std::string_view::npos )
			return invalid_cookie<cookie_pair>();
		const auto name = trim_ows(statement.substr(0, separator));
		const auto value = trim_ows(statement.substr(separator + 1));
		if( not valid_token(name) or not valid_cookie_value(value) )
			return invalid_cookie<cookie_pair>();
		return cookie_pair {std::string(name), riwo::value(value)};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace

sys_expected<std::vector<cookie_pair>> parse_cookie_fields(
	const fields &values) noexcept
{
	try
	{
		std::vector<cookie_pair> result;
		for( const auto &header : values.find_all("Cookie") )
		{
			size_t begin = 0;
			while( begin <= header.value->size() )
			{
				const auto end = header.value->find(';', begin);
				const auto statement = std::string_view(
					header.value.to_string()).substr(begin,
					end == std::string::npos ? std::string::npos : end - begin);
				if( trim_ows(statement).empty() )
					return invalid_cookie<std::vector<cookie_pair>>();
				auto parsed = parse_cookie_pair(statement);
				if( not parsed )
					return sys_unexpected(parsed.error());
				result.emplace_back(std::move(*parsed));
				if( end == std::string::npos )
					break;
				begin = end + 1;
			}
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<response_cookie> parse_set_cookie(std::string_view value) noexcept
{
	try
	{
		const auto first_end = value.find(';');
		auto first = parse_cookie_pair(value.substr(0, first_end));
		if( not first )
			return sys_unexpected(first.error());
		response_cookie result {
			.name = std::move(first->name), .value = std::move(first->value)
		};
		size_t begin = first_end == std::string_view::npos ?
			value.size() : first_end + 1;
		while( begin < value.size() )
		{
			const auto end = value.find(';', begin);
			auto statement = trim_ows(value.substr(begin,
				end == std::string_view::npos ? std::string_view::npos : end - begin));
			begin = end == std::string_view::npos ? value.size() : end + 1;
			if( statement.empty() )
				continue;
			const auto separator = statement.find('=');
			const auto attribute_name = trim_ows(statement.substr(0, separator));
			const auto attribute_value = separator == std::string_view::npos ?
				std::string_view{} : trim_ows(statement.substr(separator + 1));
			if( not valid_token(attribute_name) )
				continue;
			const auto name = lower(attribute_name);
			if( name == "secure" )
				result.secure = true;
			else if( name == "httponly" )
				result.http_only = true;
			else if( name == "domain" )
			{
				if( attribute_value.empty() )
					continue;
				auto domain = lower(attribute_value);
				while( domain.starts_with('.') ) domain.erase(domain.begin());
				if( not domain.empty() )
					result.domain = std::move(domain);
			}
			else if( name == "path" )
			{
				if( not attribute_value.empty() )
					result.path = std::string(attribute_value);
			}
			else if( name == "expires" )
			{
				if( auto parsed = parse_http_date(attribute_value) )
					result.expires = *parsed;
			}
			else if( name == "max-age" )
			{
				if( auto parsed = parse_max_age(attribute_value) )
					result.max_age = *parsed;
			}
			else if( name == "samesite" )
			{
				const auto mode = lower(attribute_value);
				if( mode == "strict" )
					result.same_site = cookie_same_site::strict;
				else if( mode == "lax" )
					result.same_site = cookie_same_site::lax;
				else if( mode == "none" )
					result.same_site = cookie_same_site::none;
			}
			else if( valid_attribute_value(attribute_value) )
			{
				cookie_extension extension {.name = std::string(attribute_name)};
				if( separator != std::string_view::npos )
					extension.value = riwo::value(attribute_value);
				result.extensions.emplace_back(std::move(extension));
			}
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

std::string serialize_cookie(std::span<const cookie_pair> values)
{
	std::string result;
	for( const auto &item : values )
	{
		const auto &value = item.value.to_string();
		if( not valid_token(item.name) or not valid_cookie_value(value) )
			throw std::invalid_argument("Invalid HTTP Cookie pair");
		if( not result.empty() )
			result += "; ";
		result += item.name;
		result += '=';
		result += value;
	}
	return result;
}

std::string serialize_set_cookie(const response_cookie &value)
{
	std::array<cookie_pair,1> pair {{cookie_pair {value.name, value.value}}};
	auto result = serialize_cookie(pair);
	if( value.expires )
		result += "; Expires=" + format_http_date(*value.expires);
	if( value.max_age )
		result += "; Max-Age=" + std::to_string(value.max_age->count());
	if( value.domain )
	{
		if( not valid_domain(*value.domain) )
			throw std::invalid_argument("Invalid Cookie Domain");
		result += "; Domain=" + *value.domain;
	}
	if( value.path )
	{
		if( value.path->empty() or value.path->front() != '/' or
			not valid_attribute_value(*value.path) )
			throw std::invalid_argument("Invalid Cookie Path");
		result += "; Path=" + *value.path;
	}
	if( value.secure )
		result += "; Secure";
	if( value.http_only )
		result += "; HttpOnly";
	switch(value.same_site)
	{
	case cookie_same_site::strict: result += "; SameSite=Strict"; break;
	case cookie_same_site::lax: result += "; SameSite=Lax"; break;
	case cookie_same_site::none: result += "; SameSite=None"; break;
	case cookie_same_site::unspecified: break;
	}
	for( const auto &extension : value.extensions )
	{
		if( not valid_token(extension.name) )
			throw std::invalid_argument("Invalid Cookie extension name");
		result += "; " + extension.name;
		if( extension.value )
		{
			const auto &text = extension.value->to_string();
			if( not valid_attribute_value(text) )
				throw std::invalid_argument("Invalid Cookie extension value");
			result += '=';
			result += text;
		}
	}
	return result;
}

void add_set_cookie(fields &headers, const response_cookie &value) {
	headers.add("Set-Cookie", serialize_set_cookie(value));
}

} //namespace riwo::http_exp
