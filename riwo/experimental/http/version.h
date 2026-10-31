// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_VERSION_H
#define RIWO_EXP_HTTP_VERSION_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

enum class protocol
{
	v1_0, v1_1, v1 = v1_1,
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2_0, v2 = v2_0
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_VERSION_H
