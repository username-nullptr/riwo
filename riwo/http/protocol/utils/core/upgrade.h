// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_UPGRADE_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_UPGRADE_H

#include <riwo/http/protocol/header.h>
#include <riwo/http/protocol/types.h>

namespace riwo::http
{

[[nodiscard]] RIWO_HTTP_API bool header_has_token (
	const headers &values, std::string_view field, std::string_view token
) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
is_upgrade_request(const headers &values) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
is_upgrade_response(status_enum status, const headers &values) noexcept;

[[nodiscard]] RIWO_HTTP_API std::optional<std::string>
upgrade_protocol(const headers &values) noexcept;

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_UPGRADE_H
