// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CLIENT_REQUEST_ARG_H
#define RIWO_HTTP_PROTOCOL_UTILS_CLIENT_REQUEST_ARG_H

#include <riwo/http/protocol/utils/core/container_helper.h>

namespace riwo::http
{

class RIWO_HTTP_API request_arg final :
	public mutable_headers<request_arg>,
	public mutable_cookies<value,request_arg>,
	public mutable_chunk_attributes<request_arg>
{
public:
	request_arg();
	~request_arg() override;

	request_arg(const request_arg &other) noexcept;
	request_arg &operator=(const request_arg &other) noexcept;

	request_arg(request_arg &&other) noexcept;
	request_arg &operator=(request_arg &&other) noexcept;

public:
	request_arg &set_basic_auth (
		std::string_view username, std::string_view password
	);
	request_arg &set_bearer_auth (
		std::string_view token
	);
	request_arg &set_proxy_basic_auth (
		std::string_view username, std::string_view password
	);

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo::http
#include <riwo/http/protocol/utils/client/detail/request_arg.h>


#endif //RIWO_HTTP_PROTOCOL_UTILS_CLIENT_REQUEST_ARG_H
