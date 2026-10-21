// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_COOKIE_H
#define RIWO_HTTP_EXP_PROTOCOL_COOKIE_H

#include <riwo/http_exp/protocol/fields.h>
#include <riwo/core/value.h>

namespace riwo::http_exp
{

struct cookie_pair
{
	std::string name {};
	riwo::value value {};

	friend bool operator==(const cookie_pair &, const cookie_pair &) = default;
};

enum class cookie_same_site
{
	unspecified,
	strict,
	lax,
	none
};

struct cookie_extension
{
	std::string name {};
	optional<riwo::value> value {};

	friend bool operator==(const cookie_extension &,
		const cookie_extension &) = default;
};

struct response_cookie
{
	std::string name {};
	riwo::value value {};
	optional<std::string> domain {};
	optional<std::string> path {};
	optional<std::chrono::system_clock::time_point> expires {};
	optional<std::chrono::seconds> max_age {};
	cookie_same_site same_site = cookie_same_site::unspecified;
	bool secure = false;
	bool http_only = false;
	std::vector<cookie_extension> extensions {};

	friend bool operator==(const response_cookie &,
		const response_cookie &) = default;
};

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<std::vector<cookie_pair>> parse_cookie_fields(
	const fields &values) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<response_cookie> parse_set_cookie(
	std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
std::string serialize_cookie(std::span<const cookie_pair> values);

[[nodiscard]] RIWO_HTTP_EXP_API
std::string serialize_set_cookie(const response_cookie &value);

RIWO_HTTP_EXP_API void add_set_cookie(fields &headers,
	const response_cookie &value);

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_COOKIE_H
