// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_REQUEST_H
#define RIWO_HTTP_EXP_SERVER_REQUEST_H

#include <riwo/http_exp/message.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API server_request
{
public:
	server_request(http_exp::method request_method = http_exp::method::get,
		std::string target = "/", fields request_headers = {});

	[[nodiscard]] http_exp::method method() const noexcept { return method_; }
	[[nodiscard]] std::string_view target() const noexcept { return target_; }
	[[nodiscard]] const fields &headers() const noexcept { return headers_; }

private:
	http_exp::method method_;
	std::string target_;
	http_exp::fields headers_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_REQUEST_H
