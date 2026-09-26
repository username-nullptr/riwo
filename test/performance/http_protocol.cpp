// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/http/cxx/configs.h>
#include <riwo/http/protocol/utils/client/generator.h>
#include <riwo/http/protocol/utils/core/compression.h>
#include <riwo/http/protocol/utils/server/parser.h>

namespace
{

#ifdef NDEBUG
constexpr size_t protocol_cycle_count = 250'000 * riwo::test::performance_scale;
constexpr size_t gzip_cycle_count = 2'000 * riwo::test::performance_scale;
#else
constexpr size_t protocol_cycle_count = 10'000 * riwo::test::performance_scale;
constexpr size_t gzip_cycle_count = 100 * riwo::test::performance_scale;
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

duration_t measure_request_parser(size_t count)
{
	using namespace riwo::http;
	constexpr std::string_view request =
		"POST /api/items?q=42 HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 16\r\n\r\n"
		"0123456789abcdef";
	server_parser parser(256);
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto parsed = parser.append(riwo::buffer(request));
		RIWO_TEST_CHECK(parsed and *parsed);
		checksum += parser.path().size() + parser.take_body().size();
		parser.reset();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum, count * size_t {26});
	return elapsed;
}

duration_t measure_request_generator(size_t count)
{
	using namespace riwo::http;
	const riwo::url target("http://example.test/api/items?q=42");
	request_arg argument;
	argument.set_header("Content-Type", "text/plain");
	constexpr std::string_view body = "0123456789abcdef";
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		client_generator generator(target, argument);
		auto output = generator.header_data(method::post, body.size());
		output += generator.body_data(riwo::buffer(body));
		checksum += output.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * body.size());
	return elapsed;
}

#if RIWO_HTTP_ZLIB_SUPPORT
duration_t measure_gzip_round_trip(size_t count)
{
	std::string payload(64 * 1024, 'a');
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		payload[0] = static_cast<char>(index);
		auto compressed = riwo::http::gzip_compress(payload);
		RIWO_TEST_CHECK(compressed);
		auto decoded = riwo::http::gzip_decompress(*compressed, payload.size());
		RIWO_TEST_CHECK(decoded);
		checksum += decoded->size() + static_cast<unsigned char>((*decoded)[0]);
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum >= count * payload.size());
	return elapsed;
}
#endif

void protocol_hot_paths()
{
	riwo::test::print_performance_result(
		"HTTP/request parse with 16 B body (median of 3)", protocol_cycle_count,
		median_duration(protocol_cycle_count, measure_request_parser), "message"
	);
	riwo::test::print_performance_result(
		"HTTP/request generate with 16 B body (median of 3)", protocol_cycle_count,
		median_duration(protocol_cycle_count, measure_request_generator), "message"
	);
#if RIWO_HTTP_ZLIB_SUPPORT
	riwo::test::print_performance_result(
		"HTTP/gzip encode + decode 64 KiB (median of 3)", gzip_cycle_count,
		median_duration(gzip_cycle_count, measure_gzip_round_trip), "cycle"
	);
#endif
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP protocol hot paths", protocol_hot_paths},
	});
}
