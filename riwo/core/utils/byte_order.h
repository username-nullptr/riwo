// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_BYTE_ORDER_H
#define RIWO_CORE_UTILS_BYTE_ORDER_H

#include <riwo/core/cxx/type_traits.h>
#include <riwo/core/cxx/attributes.h>
#include <riwo/core/cxx/concepts.h>

namespace riwo
{

[[nodiscard]] RIWO_CORE_VAPI bool is_little_endian();
[[nodiscard]] RIWO_CORE_VAPI bool is_big_endian();

[[nodiscard]] RIWO_CORE_TAPI auto hton(concepts::arithmetic_p auto t);
[[nodiscard]] RIWO_CORE_TAPI auto hton(concepts::enumerate_p auto e);
RIWO_CORE_TAPI auto *hton(auto *data, size_t len = 1);

[[nodiscard]] RIWO_CORE_TAPI auto ntoh(concepts::arithmetic_p auto t);
[[nodiscard]] RIWO_CORE_TAPI auto ntoh(concepts::enumerate_p auto e);
RIWO_CORE_TAPI auto *ntoh(auto *data, size_t len = 1);

[[nodiscard]] RIWO_CORE_TAPI auto reverse(concepts::arithmetic_p auto t);
[[nodiscard]] RIWO_CORE_TAPI auto reverse(concepts::enumerate_p auto e);
RIWO_CORE_TAPI auto *reverse(auto *data, size_t len = 1);

[[nodiscard]] RIWO_CORE_TAPI auto to_big_endian(concepts::arithmetic_p auto t);
[[nodiscard]] RIWO_CORE_TAPI auto to_big_endian(concepts::enumerate_p auto e);
RIWO_CORE_TAPI auto *to_big_endian(auto *data, size_t len = 1);

[[nodiscard]] RIWO_CORE_TAPI auto to_little_endian(concepts::arithmetic_p auto t);
[[nodiscard]] RIWO_CORE_TAPI auto to_little_endian(concepts::enumerate_p auto e);
RIWO_CORE_TAPI auto *to_little_endian(auto *data, size_t len = 1);

} //namespace riwo
#include <riwo/core/utils/detail/byte_order.h>


#endif //RIWO_CORE_UTILS_BYTE_ORDER_H
