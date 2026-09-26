// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_MATH_H
#define RIWO_CORE_ALGORITHM_MATH_H

#include <riwo/core/global.h>
#include <cmath>

namespace riwo { namespace concepts
{

template <typename Iter, typename Func>
concept mean_value_projection = requires(Iter it, Func &func) {
	{ *func(*it) } -> arithmetic_p;
};

template <typename Iter, typename Func>
concept mean_iterator_projection = requires(Iter it, Func &func) {
	{ *func(it) } -> arithmetic_p;
};

} //namespace concepts

template <typename Iter>
[[nodiscard]] RIWO_CORE_TAPI auto mean(Iter begin, Iter end) requires
	concepts::arithmetic_p<decltype(*begin)>;

template <typename Iter, typename Func>
[[nodiscard]] RIWO_CORE_TAPI auto mean(Iter begin, Iter end, Func &&func) requires (
	concepts::mean_value_projection<Iter,Func> or concepts::mean_iterator_projection<Iter,Func>
);

} //namespace riwo
#include <riwo/core/algorithm/detail/math.h>


#endif //RIWO_CORE_ALGORITHM_MATH_H
