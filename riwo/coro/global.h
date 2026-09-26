// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORO_GLOBAL_H
#define RIWO_CORO_GLOBAL_H

#include <riwo/core/execution.h>

#if RIWO_BUILD_STATIC
# define RIWO_CORO_API
#elif defined(RIWO_CORO_SHARED)
# ifdef riwo_coro_EXPORTS
#  define RIWO_CORO_API  RIWO_DECL_EXPORT
# else //riwo_coro_EXPORTS
#  define RIWO_CORO_API  RIWO_DECL_IMPORT
# endif //riwo_coro_EXPORTS

#else //RIWO_CORO_SHARED
# define RIWO_CORO_API
#endif //RIWO_CORO_SHARED

#define RIWO_CORO_VAPI  RIWO_CORE_VAPI
#define RIWO_CORO_TAPI  RIWO_CORE_TAPI


#endif //RIWO_CORO_GLOBAL_H
