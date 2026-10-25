// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_AUTHENTICATION_H
#define RIWO_HTTP_EXP_PROTOCOL_AUTHENTICATION_H

#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp
{

struct basic_credentials
{
	std::string username;
	std::string password;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::string make_basic_authorization(std::string_view username, std::string_view password);

[[nodiscard]] RIWO_HTTP_EXP_API
std::string make_bearer_authorization(std::string_view token);

[[nodiscard]] RIWO_HTTP_EXP_API
optional<basic_credentials> parse_basic_authorization(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
optional<std::string> parse_bearer_authorization(std::string_view value) noexcept;

RIWO_HTTP_EXP_API void set_basic_authorization (
	fields &headers, std::string_view username, std::string_view password,
	bool proxy = false
);

RIWO_HTTP_EXP_API void set_bearer_authorization (
	fields &headers, std::string_view token, bool proxy = false
);

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_AUTHENTICATION_H
