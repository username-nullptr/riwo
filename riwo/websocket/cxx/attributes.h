// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_CXX_ATTRIBUTES_H
#define RIWO_WEBSOCKET_CXX_ATTRIBUTES_H

#include <riwo/http/global.h>

#if RIWO_BUILD_STATIC
# define RIWO_WEBSOCKET_API
#elif defined(RIWO_WEBSOCKET_SHARED)
# ifdef riwo_websocket_EXPORTS
#  define RIWO_WEBSOCKET_API  RIWO_DECL_EXPORT
# else //riwo_websocket_EXPORTS
#  define RIWO_WEBSOCKET_API  RIWO_DECL_IMPORT
# endif //riwo_websocket_EXPORTS

#else //RIWO_WEBSOCKET_SHARED
# define RIWO_WEBSOCKET_API
#endif //RIWO_WEBSOCKET_SHARED

#define RIWO_WEBSOCKET_VAPI  RIWO_CORE_VAPI
#define RIWO_WEBSOCKET_TAPI  RIWO_CORE_TAPI


#endif //RIWO_WEBSOCKET_CXX_ATTRIBUTES_H
