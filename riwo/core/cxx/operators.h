// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_OPERATOR_H
#define RIWO_CORE_CXX_OPERATOR_H

#include <riwo/core/cxx/attributes.h>
#include <riwo/core/cxx/concepts.h>

namespace riwo
{

[[nodiscard]] constexpr bool equality (
	concepts::arithmetic_p auto a, concepts::arithmetic_p auto b
);

[[nodiscard]] constexpr bool nequality (
	concepts::arithmetic_p auto a, concepts::arithmetic_p auto b
);

[[nodiscard]] constexpr bool equal_greater (
	concepts::arithmetic_p auto a, concepts::arithmetic_p auto b
);

[[nodiscard]] constexpr bool equal_less (
	concepts::arithmetic_p auto a, concepts::arithmetic_p auto b
);

} //namespace riwo
#include <riwo/core/cxx/detail/operators.h>


#endif //RIWO_CORE_CXX_OPERATOR_H
