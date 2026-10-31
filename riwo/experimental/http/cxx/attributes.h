// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CXX_ATTRIBUTES_H
#define RIWO_EXP_HTTP_CXX_ATTRIBUTES_H

#include <riwo/core/global.h>

#if RIWO_BUILD_STATIC
# define RIWO_EXP_HTTP_API
#elif defined(riwo_exp_http_EXPORTS)
# define RIWO_EXP_HTTP_API  RIWO_DECL_EXPORT
#else //RIWO_BUILD_STATIC
# define RIWO_EXP_HTTP_API  RIWO_DECL_IMPORT
#endif //RIWO_BUILD_STATIC

#define RIWO_EXP_HTTP_VAPI  RIWO_CORE_VAPI
#define RIWO_EXP_HTTP_TAPI  RIWO_CORE_TAPI


#endif //RIWO_EXP_HTTP_CXX_ATTRIBUTES_H
