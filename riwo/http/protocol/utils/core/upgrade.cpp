// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "upgrade.h"
#include <riwo/core/string_vector.h>

namespace riwo::http
{

bool header_has_token
(const headers &values, std::string_view field, std::string_view token) noexcept
{
	auto it = values.find(std::string(field));
	if( it == values.end() )
		return false;

	auto wanted = strtls::to_lower(strtls::trimmed(token));
	return std::ranges::any_of (
		string_vector::from_string(it->second.to_string(), ','),
		[&](const auto &item) {
			return strtls::to_lower(strtls::trimmed(item)) == wanted;
		}
	);
}

std::optional<std::string> upgrade_protocol(const headers &values) noexcept
{
	auto it = values.find(header::upgrade);
	if( it == values.end() )
		return std::nullopt;

	auto value = strtls::trimmed(it->second.to_string());
	if( value.empty() )
		return std::nullopt;
	return value;
}

bool is_upgrade_request(const headers &values) noexcept
{
	return header_has_token(values, header::connection, "upgrade") and
		   upgrade_protocol(values).has_value();
}

bool is_upgrade_response(status_enum status, const headers &values) noexcept
{
	return status == status::switching_protocols and is_upgrade_request(values);
}

} //namespace riwo::http
