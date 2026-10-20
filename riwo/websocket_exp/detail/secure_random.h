// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_SECURE_RANDOM_H
#define RIWO_WEBSOCKET_EXP_DETAIL_SECURE_RANDOM_H

#include <riwo/websocket_exp/global.h>

namespace riwo::websocket_exp::detail
{

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<> secure_random_bytes(const mutable_buffer &output) noexcept;

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_SECURE_RANDOM_H
