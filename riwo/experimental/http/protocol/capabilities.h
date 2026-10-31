// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_CAPABILITIES_H
#define RIWO_EXP_HTTP_PROTOCOL_CAPABILITIES_H

namespace riwo::experimental::http
{

struct stream_capabilities
{
	bool trailers = false;
	bool full_duplex = false;
	bool tunnel = false;
	bool extended_connect = false;
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_CAPABILITIES_H
