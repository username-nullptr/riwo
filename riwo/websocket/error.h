// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_ERROR_H
#define RIWO_WEBSOCKET_ERROR_H

#include <riwo/websocket/global.h>

namespace riwo::websocket
{

#define RIWO_WEBSOCKET_ERRC_TABLE \
X_MACRO( handshake_rejected       ,  1 , "WebSocket handshake rejected"      ) \
X_MACRO( invalid_upgrade          ,  2 , "Invalid WebSocket upgrade"         ) \
X_MACRO( invalid_accept_key       ,  3 , "Invalid WebSocket accept key"      ) \
X_MACRO( unsupported_version      ,  4 , "Unsupported WebSocket version"     ) \
X_MACRO( unsupported_subprotocol  ,  5 , "Unsupported WebSocket subprotocol" ) \
X_MACRO( unsupported_extension    ,  6 , "Unsupported WebSocket extension"   ) \
X_MACRO( redirect_limit_exceeded  ,  7 , "WebSocket redirect limit exceeded" ) \
X_MACRO( insecure_redirect        ,  8 , "Insecure WebSocket redirect"       ) \
X_MACRO( message_too_big          ,  9 , "WebSocket message too big"         ) \
X_MACRO( write_queue_full         , 10 , "WebSocket write queue full"        ) \
X_MACRO( already_open             , 11 , "WebSocket stream already open"     ) \
X_MACRO( not_open                 , 12 , "WebSocket stream is not open"      ) \
X_MACRO( closing                  , 13 , "WebSocket stream is closing"       ) \
X_MACRO( closed                   , 14 , "WebSocket stream is closed"        )

enum class errc
{
#define X_MACRO(e,v,d) e = (v),
	RIWO_WEBSOCKET_ERRC_TABLE
#undef X_MACRO
};

[[nodiscard]] RIWO_WEBSOCKET_API
const error_category_t &error_category() noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
error_code make_error_code(errc value) noexcept;

} //namespace riwo::websocket

template <>
struct std::is_error_code_enum<riwo::websocket::errc> : true_type {};


#endif //RIWO_WEBSOCKET_ERROR_H
