// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_CAPABILITIES_H
#define RIWO_HTTP_PROTOCOL_CAPABILITIES_H

namespace riwo::http
{

struct stream_capabilities
{
	bool trailers = false;
	bool full_duplex = false;
	bool tunnel = false;
	bool extended_connect = false;
};

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_CAPABILITIES_H
