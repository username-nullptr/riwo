// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_SYS_EXPECTED_H
#define RIWO_CORE_CXX_SYS_EXPECTED_H

#include <riwo/core/cxx/expected.h>

namespace riwo
{

template <concepts::expected_value Value = void>
using sys_expected = expected<Value,error_code>;

using sys_unexpected = unexpected<error_code>;

template <concepts::optional_value_p Value>
[[nodiscard]] RIWO_CORE_TAPI auto make_sys_expected(Value &&value);
[[nodiscard]] RIWO_CORE_VAPI sys_expected<> make_sys_expected();

template <concepts::expected_value Value = void>
RIWO_CORE_TAPI void sys_expected_loc_throw(const sys_expected<Value> &expected,
	std::source_location loc = std::source_location::current()
);

template <concepts::expected_value Value = void>
RIWO_CORE_TAPI void sys_expected_loc_throw(const sys_expected<Value> &expected,
	concepts::text_p<char> auto &&msg, std::source_location loc = std::source_location::current()
);

using io_expected = sys_expected<size_t>;
using io_unexpected = sys_unexpected;

[[nodiscard]] RIWO_CORE_TAPI io_expected make_io_expected(size_t sum);

} //namespace riwo
#include <riwo/core/cxx/detail/sys_expected.h>


#endif //RIWO_CORE_CXX_SYS_EXPECTED_H
