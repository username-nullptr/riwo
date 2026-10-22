// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_BASE64_H
#define RIWO_CORE_ALGORITHM_BASE64_H

#include <riwo/core/global.h>

namespace riwo
{

[[nodiscard]] RIWO_CORE_API
std::string base64_encode(std::span<const std::byte> input);

[[nodiscard]] RIWO_CORE_API
std::string base64_encode(std::string_view input);

} //namespace riwo


#endif //RIWO_CORE_ALGORITHM_BASE64_H
