// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CLIENT_DETAIL_GENERATOR_H
#define RIWO_HTTP_PROTOCOL_UTILS_CLIENT_DETAIL_GENERATOR_H

namespace riwo::http
{

template <method_enum Method>
std::string generator<protocol_model::client>::header_data(size_t body_size)
{
	return header_data(Method, body_size);
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CLIENT_DETAIL_GENERATOR_H
