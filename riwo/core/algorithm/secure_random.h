// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_SECURE_RANDOM_H
#define RIWO_CORE_ALGORITHM_SECURE_RANDOM_H

#include <riwo/core/global.h>

namespace riwo
{

[[nodiscard]] RIWO_CORE_API
sys_expected<> secure_random_bytes(const mutable_buffer &output) noexcept;

} //namespace riwo


#endif //RIWO_CORE_ALGORITHM_SECURE_RANDOM_H
