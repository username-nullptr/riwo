// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request_head.h"
#include <riwo/core/utils/string_tools.h>

#include <utility>

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool equal_ascii_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

[[nodiscard]] optional<uint64_t> parse_content_length(std::string_view value) noexcept
{
	uint64_t result = 0;
	const auto parsed = std::from_chars (
		value.data(), value.data() + value.size(), result
	);
	if( parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size() )
		return {};
	return result;
}

} //namespace

optional<std::string_view> request_head::header(std::string_view name) const noexcept
{
	return headers.find_first(name);
}

bool request_head::has_header(std::string_view name) const noexcept
{
	return headers.find_first(name).has_value();
}

bool request_head::contains_header(std::string_view name) const noexcept
{
	return has_header(name);
}

request_head &request_head::set_header(std::string name, riwo::value value)
{
	headers.set(std::move(name), std::move(value));
	return *this;
}

request_head &request_head::add_header(std::string name, riwo::value value)
{
	headers.add(std::move(name), std::move(value));
	return *this;
}

request_head &request_head::erase_header(std::string_view name) noexcept
{
	headers.erase(name);
	return *this;
}

request_head &request_head::unset_header(std::string_view name) noexcept
{
	return erase_header(name);
}

request_head &request_head::set_cookies(std::span<const cookie_pair> values)
{
	if( values.empty() )
	{
		headers.erase("Cookie");
		return *this;
	}
	auto serialized = serialize_cookie(values);
	headers.set("Cookie", std::move(serialized));
	return *this;
}

request_head &request_head::add_cookie(std::string name, riwo::value value)
{
	return add_cookie ({
		.name = std::move(name), .value = std::move(value)
	});
}

request_head &request_head::add_cookie(cookie_pair value)
{
	auto values = parse_cookie_fields(headers);
	sys_expected_loc_throw(values, "Invalid HTTP Cookie fields");
	values->emplace_back(std::move(value));
	return set_cookies(*values);
}

optional<std::string_view> request_head::content_type() const noexcept
{
	return header("Content-Type");
}

optional<uint64_t> request_head::content_length() const noexcept
{
	const auto value = header("Content-Length");
	return value ? parse_content_length(*value) : optional<uint64_t>{};
}

std::string_view request_head::host() const noexcept
{
	if( not authority.empty() )
		return authority;
	if( auto value = header("Host") )
		return *value;
	return url.host();
}

std::string_view request_head::path() const noexcept
{
	if( not target.empty() )
		return target;
	const auto value = url.path();
	return value.empty() ? std::string_view("/") : value;
}

bool request_head::is_http() const noexcept
{
	const auto value = scheme.empty() ?
		url.protocol() : std::string_view(scheme);
	return equal_ascii_case_insensitive(value, "http");
}

bool request_head::is_https() const noexcept
{
	const auto value = scheme.empty() ?
		url.protocol() : std::string_view(scheme);
	return equal_ascii_case_insensitive(value, "https");
}

bool request_head::is_upgrade() const noexcept
{
	return upgrade_protocol.has_value();
}

} //namespace riwo::http_exp
