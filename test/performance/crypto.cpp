// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/crypto/sha1.h>

namespace
{

#ifdef NDEBUG
constexpr size_t hash_cycle_count = 20'000 * riwo::test::performance_scale;
#else
constexpr size_t hash_cycle_count = 1'000 * riwo::test::performance_scale;
#endif

using duration_t = std::chrono::steady_clock::duration;

template <typename Func>
duration_t median_duration(size_t count, Func &&func)
{
	func(std::max<size_t>(1, count / 20));
	std::array<duration_t,3> samples {};
	for(auto &sample : samples)
		sample = func(count);
	std::ranges::sort(samples);
	return samples[1];
}

duration_t measure_sha1(size_t count)
{
	std::string payload(4096, 'x');
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		payload[0] = static_cast<char>(index);
		auto digest = riwo::crypto::sha1(payload).finalize().hex(false);
		checksum += digest.size() + static_cast<unsigned char>(digest[0]);
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * 40);
	return elapsed;
}

void crypto_hot_paths()
{
	riwo::test::print_performance_result(
		"crypto/SHA-1 4 KiB (median of 3)", hash_cycle_count,
		median_duration(hash_cycle_count, measure_sha1), "digest"
	);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"crypto hot paths", crypto_hot_paths},
	});
}
