// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_RIWO_H
#define RIWO_RIWO_H

#include <riwo/core.h>

#if RIWO_CORO_SUPPORT
# include <riwo/coro.h>
#endif //RIWO_CORO_SUPPORT

#if RIWO_HTTP_SUPPORT
# include <riwo/http.h>
#endif //RIWO_HTTP_SUPPORT

#if RIWO_WEBSOCKET_SUPPORT
# include <riwo/websocket.h>
#endif //RIWO_WEBSOCKET_SUPPORT

#if RIWO_UTILITIES_SUPPORT
# include <riwo/utils.h>
#endif //RIWO_UTILITIES_SUPPORT

#if RIWO_EXP_HTTP_SUPPORT
# include <riwo/experimental/http.h>
#endif //RIWO_EXP_HTTP_SUPPORT

#if RIWO_EXP_WEBSOCKET_SUPPORT
# include <riwo/experimental/websocket.h>
#endif //RIWO_EXP_WEBSOCKET_SUPPORT


#endif //RIWO_RIWO_H
