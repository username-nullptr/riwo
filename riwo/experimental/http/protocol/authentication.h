// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_AUTHENTICATION_H
#define RIWO_EXP_HTTP_PROTOCOL_AUTHENTICATION_H

#include <riwo/experimental/http/protocol/fields.h>

namespace riwo::experimental::http
{

struct basic_credentials
{
	std::string username;
	std::string password;
};

[[nodiscard]] RIWO_EXP_HTTP_API
std::string make_basic_authorization(std::string_view username, std::string_view password);

[[nodiscard]] RIWO_EXP_HTTP_API
std::string make_bearer_authorization(std::string_view token);

[[nodiscard]] RIWO_EXP_HTTP_API
optional<basic_credentials> parse_basic_authorization(std::string_view value) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API
optional<std::string> parse_bearer_authorization(std::string_view value) noexcept;

RIWO_EXP_HTTP_API void set_basic_authorization (
	fields &headers, std::string_view username, std::string_view password,
	bool proxy = false
);

RIWO_EXP_HTTP_API void set_bearer_authorization (
	fields &headers, std::string_view token, bool proxy = false
);

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_AUTHENTICATION_H
