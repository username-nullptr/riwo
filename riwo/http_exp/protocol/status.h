// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_STATUS_H
#define RIWO_HTTP_EXP_PROTOCOL_STATUS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API status
{
public:
	enum well_known : uint16_t
	{
		continue_upload = 100,
		switching_protocols = 101,
		ok = 200,
		created = 201,
		no_content = 204,
		moved_permanently = 301,
		found = 302,
		not_modified = 304,
		bad_request = 400,
		unauthorized = 401,
		forbidden = 403,
		not_found = 404,
		internal_server_error = 500,
		bad_gateway = 502,
		service_unavailable = 503
	};

	status(well_known value) noexcept;
	explicit status(uint16_t code);

	[[nodiscard]] uint16_t code() const noexcept;

	friend bool operator==(status, status) noexcept = default;

private:
	uint16_t code_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_STATUS_H
