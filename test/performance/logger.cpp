// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/utils/logger.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <thread>
#include <vector>

namespace
{

using logger_t = riwo::utils::logger;
using duration_t = std::chrono::steady_clock::duration;

#ifdef NDEBUG
constexpr size_t hot_path_count = 500'000 * riwo::test::performance_scale;
constexpr size_t file_log_count = 10'000 * riwo::test::performance_scale;
#else
constexpr size_t hot_path_count = 10'000 * riwo::test::performance_scale;
constexpr size_t file_log_count = 500 * riwo::test::performance_scale;
#endif

constexpr size_t thread_count = 4;
constexpr std::string_view disabled_logger_name = "riwo-performance-logger-disabled";
constexpr std::string_view file_logger_name = "riwo-performance-logger-file";

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

duration_t measure_formatting(size_t count)
{
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto message = std::format("request {} payload {}", index, 64);
		checksum += message.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count);
	return elapsed;
}

duration_t measure_cached_literal(logger_t &logger, size_t count)
{
	const logger_t::source_loc location(__FILE__, __func__, __LINE__);
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
		logger.info(location, "request payload");
	return std::chrono::steady_clock::now() - begin;
}

duration_t measure_cached_formatted(logger_t &logger, size_t count)
{
	const logger_t::source_loc location(__FILE__, __func__, __LINE__);
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
		logger.info(location, "request {} payload {}", index, 64);
	return std::chrono::steady_clock::now() - begin;
}

duration_t measure_named_formatted(size_t count)
{
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		riwo_utils_clog_info(
			disabled_logger_name, "request {} payload {}", index, 64
		);
	}
	return std::chrono::steady_clock::now() - begin;
}

duration_t measure_parallel_named_formatted(size_t count)
{
	std::atomic_size_t ready {0};
	std::atomic_size_t completed {0};
	std::atomic_bool start {false};
	std::vector<std::thread> threads;
	threads.reserve(thread_count);

	for(size_t thread_index = 0; thread_index < thread_count; ++thread_index)
	{
		const auto thread_operations = count / thread_count +
			(thread_index < count % thread_count ? 1 : 0);
		threads.emplace_back([&, thread_operations]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();

			for(size_t index = 0; index < thread_operations; ++index)
			{
				riwo_utils_clog_info(
					disabled_logger_name, "request {} payload {}", index, 64
				);
			}
			completed.fetch_add(thread_operations, std::memory_order_relaxed);
		});
	}

	while(ready.load(std::memory_order_acquire) != thread_count)
		std::this_thread::yield();
	const auto begin = std::chrono::steady_clock::now();
	start.store(true, std::memory_order_release);
	for(auto &thread : threads)
		thread.join();
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	RIWO_TEST_CHECK_EQ(completed.load(std::memory_order_relaxed), count);
	return elapsed;
}

void disabled_hot_paths()
{
	auto &logger = logger_t::instance(disabled_logger_name);
	logger_t::config_t config;
	config.level.console = logger_t::level_t::off;
	config.level.daily = logger_t::level_t::off;
	logger.set_config(config);

	riwo::test::print_performance_result(
		"logger/std::format baseline (median of 3)", hot_path_count,
		median_duration(hot_path_count, measure_formatting), "format"
	);
	riwo::test::print_performance_result(
		"logger/disabled cached literal (median of 3)", hot_path_count,
		median_duration(hot_path_count, [&](size_t count) {
			return measure_cached_literal(logger, count);
		}), "call"
	);
	riwo::test::print_performance_result(
		"logger/disabled cached formatted (median of 3)", hot_path_count,
		median_duration(hot_path_count, [&](size_t count) {
			return measure_cached_formatted(logger, count);
		}), "call"
	);
	riwo::test::print_performance_result(
		"logger/disabled named formatted (median of 3)", hot_path_count,
		median_duration(hot_path_count, measure_named_formatted), "call"
	);
	riwo::test::print_performance_result(
		"logger/disabled named formatted 4 threads (median of 3)", hot_path_count,
		median_duration(hot_path_count, measure_parallel_named_formatted), "call"
	);
}

void async_file_sink()
{
	riwo::test::temporary_directory directory;
	auto &logger = logger_t::instance(file_logger_name);
	logger_t::config_t config;
	config.path = directory.path();
	config.level.console = logger_t::level_t::off;
	config.level.daily = logger_t::level_t::info;
	logger.set_config(config);

	const auto elapsed = median_duration(file_log_count, [&](size_t count) {
		return measure_cached_formatted(logger, count);
	});

	logger.flush();
	config.path.clear();
	logger.set_config(config);
	size_t bytes_written = 0;
	for(const auto &entry : std::filesystem::recursive_directory_iterator(directory.path()))
	{
		if(entry.is_regular_file())
			bytes_written += entry.file_size();
	}
	RIWO_TEST_CHECK(bytes_written > 0);

	riwo::test::print_performance_result(
		"logger/async daily file enqueue (median of 3)",
		file_log_count, elapsed, "call"
	);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"logger disabled hot paths", disabled_hot_paths},
		{"logger async file sink", async_file_sink},
	});
}
