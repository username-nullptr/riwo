// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_SECURE_RANDOM_H
#define RIWO_WEBSOCKET_DETAIL_SECURE_RANDOM_H

#include <riwo/websocket/global.h>

namespace riwo::websocket::detail
{

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<> secure_random_bytes(const mutable_buffer &output) noexcept;

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_SECURE_RANDOM_H
