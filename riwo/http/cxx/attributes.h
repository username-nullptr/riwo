// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CXX_ATTRIBUTES_H
#define RIWO_HTTP_CXX_ATTRIBUTES_H

#include <riwo/core/global.h>

#if RIWO_BUILD_STATIC
# define RIWO_HTTP_API
#elif defined(riwo_http_EXPORTS)
# define RIWO_HTTP_API  RIWO_DECL_EXPORT
#else //riwo_http_EXPORTS
# define RIWO_HTTP_API  RIWO_DECL_IMPORT
#endif //riwo_http_EXPORTS

#define RIWO_HTTP_VAPI  RIWO_CORE_VAPI
#define RIWO_HTTP_TAPI  RIWO_CORE_TAPI


#endif //RIWO_HTTP_CXX_ATTRIBUTES_H
