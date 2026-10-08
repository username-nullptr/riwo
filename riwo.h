// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_RIWO_H
#define RIWO_RIWO_H

#include <riwo/core.h>

#if RIWO_CORO_SUPPORT
# include <riwo/coro.h>
#endif

#if RIWO_HTTP_SUPPORT
# include <riwo/http.h>
#endif

#if RIWO_WEBSOCKET_SUPPORT
# include <riwo/websocket.h>
#endif

#if RIWO_UTILITIES_SUPPORT
# include <riwo/utils.h>
#endif


#endif //RIWO_RIWO_H
