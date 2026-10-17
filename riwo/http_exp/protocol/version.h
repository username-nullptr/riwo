// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_VERSION_H
#define RIWO_HTTP_EXP_PROTOCOL_VERSION_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

enum class protocol
{
	v1_0,
	v1_1,
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	v2
#endif
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_VERSION_H
