// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_TEST_PERFORMANCE_BENCHMARK_H
#define RIWO_TEST_PERFORMANCE_BENCHMARK_H

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace riwo::test
{

#ifndef RIWO_PERFORMANCE_SCALE
# define RIWO_PERFORMANCE_SCALE 1
#endif

inline constexpr size_t performance_scale = RIWO_PERFORMANCE_SCALE;
static_assert(performance_scale > 0);

inline void print_performance_result(
	std::string_view name,
	size_t operations,
	std::chrono::steady_clock::duration elapsed,
	std::string_view unit
)
{
	if(operations == 0)
		throw std::invalid_argument("performance result requires at least one operation");
	const auto seconds = std::chrono::duration<double>(elapsed).count();
	if(seconds <= 0)
		throw std::runtime_error("performance timer did not advance");
	const auto nanoseconds_per_operation =
		std::chrono::duration<double,std::nano>(elapsed).count() /
		static_cast<double>(operations);
	const auto operations_per_second = static_cast<double>(operations) / seconds;

	std::cout << std::fixed << std::setprecision(2)
		<< "[PERF] " << name
		<< ": " << operations_per_second << ' ' << unit << "/s, "
		<< nanoseconds_per_operation << " ns/" << unit << ", "
		<< std::chrono::duration<double,std::milli>(elapsed).count() << " ms, "
		<< operations << ' ' << unit << " total, scale "
		<< performance_scale << '\n' << std::flush;
}

} //namespace riwo::test

#endif //RIWO_TEST_PERFORMANCE_BENCHMARK_H
