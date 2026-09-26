// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/websocket/client.h>
#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/parser.h>
#include <riwo/websocket/server.h>

namespace
{

namespace ws = riwo::websocket;

#ifdef NDEBUG
constexpr size_t codec_iteration_count = 100'000 * riwo::test::performance_scale;
constexpr size_t message_iteration_count = 2'000 * riwo::test::performance_scale;
#else
constexpr size_t codec_iteration_count = 5'000 * riwo::test::performance_scale;
constexpr size_t message_iteration_count = 200 * riwo::test::performance_scale;
#endif
constexpr size_t large_message_iteration_count = message_iteration_count / 20;
constexpr size_t warmup_count = 10;

template <typename Function>
void report_median(std::string_view name, size_t iterations, Function &&function)
{
	std::array<std::chrono::steady_clock::duration,3> samples {};
	for(auto &sample : samples)
		sample = function(iterations);
	std::ranges::sort(samples);
	riwo::test::print_performance_result(name, iterations, samples[1], "frame");
}

void frame_codec_throughput()
{
	constexpr size_t payload_size = 4 * 1024;
	const ws::masking_key key {{
		std::byte {0x12}, std::byte {0x34}, std::byte {0x56}, std::byte {0x78}
	}};
	std::vector<std::byte> source(payload_size, std::byte {0x5A});
	std::vector<std::byte> masked(payload_size);

	auto measure_mask = [&](size_t iterations)
	{
		const auto begin = std::chrono::steady_clock::now();
		for(size_t index = 0; index < iterations; ++index)
		{
			auto copied = ws::mask_copy(
				riwo::mutable_buffer(masked.data(), masked.size()),
				riwo::const_buffer(source.data(), source.size()), key);
			RIWO_TEST_CHECK(copied.has_value());
			RIWO_TEST_CHECK_EQ(*copied, payload_size);
		}
		return std::chrono::steady_clock::now() - begin;
	};
	measure_mask(warmup_count);
	report_median("WebSocket mask copy, 4 KiB (median of 3)",
		codec_iteration_count, measure_mask);
	RIWO_TEST_CHECK_EQ(masked[0], source[0] ^ key.bytes[0]);

	auto encoded = ws::encode_frame_header({
		.fin = true,
		.op = ws::opcode::binary,
		.payload_size = payload_size,
	}, ws::frame_codec_config {.local_role = ws::role::server});
	RIWO_TEST_CHECK(encoded.has_value());
	std::vector<std::byte> wire(encoded->size + source.size());
	std::memcpy(wire.data(), encoded->storage.data(), encoded->size);
	std::memcpy(wire.data() + encoded->size, source.data(), source.size());
	ws::frame_parser parser({.local_role = ws::role::client});
	auto measure_parse = [&](size_t iterations)
	{
		const auto begin = std::chrono::steady_clock::now();
		for(size_t index = 0; index < iterations; ++index)
		{
			parser.reset();
			auto parsed = parser.parse(
				riwo::mutable_buffer(wire.data(), wire.size()));
			RIWO_TEST_CHECK(parsed.has_value());
			RIWO_TEST_CHECK(parsed->frame_finished);
			RIWO_TEST_CHECK_EQ(parsed->payload.size(), payload_size);
		}
		return std::chrono::steady_clock::now() - begin;
	};
	measure_parse(warmup_count);
	report_median("WebSocket frame parse, 4 KiB (median of 3)",
		codec_iteration_count, measure_parse);
}

void message_loopback_throughput()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	service.on_connection("/benchmark",
		[](ws::accept_result accepted) -> riwo::awaitable<void>
		{
			for(;;)
			{
				auto read_result = co_await accepted.stream.read<>(
					asio::as_tuple(riwo::use_awaitable));
				auto &[read_error, message] = read_result;
				if( read_error )
					co_return;
				auto write_result = co_await accepted.stream.write(
					message.type,
					riwo::const_buffer(message.body.data(), message.body.size()),
					asio::as_tuple(riwo::use_awaitable));
				auto &[write_error, transferred] = write_result;
				riwo::ignore_unused(transferred);
				if( write_error )
					co_return;
			}
		});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::client client(context.get_executor());

	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			try
			{
				auto stream = co_await client.open(std::format(
					"ws://127.0.0.1:{}/benchmark", port),
					riwo::use_awaitable);
				const std::vector<std::byte> small(64, std::byte {0x2A});
				const std::vector<std::byte> large(64 * 1024, std::byte {0x6B});

				auto measure = [&](const std::vector<std::byte> &body,
					size_t iterations)
					-> riwo::awaitable<std::chrono::steady_clock::duration>
				{
					const auto begin = std::chrono::steady_clock::now();
					for(size_t index = 0; index < iterations; ++index)
					{
						co_await stream.write_binary(riwo::const_buffer(
							body.data(), body.size()), riwo::use_awaitable);
						auto response = co_await stream.read<>(riwo::use_awaitable);
						RIWO_TEST_CHECK_EQ(response.type, ws::message_type::binary);
						RIWO_TEST_CHECK_EQ(response.body, body);
					}
					co_return std::chrono::steady_clock::now() - begin;
				};
				auto report = [&](std::string_view name,
					const std::vector<std::byte> &body, size_t iterations)
					-> riwo::awaitable<void>
				{
					co_await measure(body, std::min(warmup_count, iterations));
					std::array<std::chrono::steady_clock::duration,3> samples {};
					for(auto &sample : samples)
						sample = co_await measure(body, iterations);
					std::ranges::sort(samples);
					riwo::test::print_performance_result(
						name, iterations, samples[1], "message");
				};

				co_await report("WebSocket loopback, 64 B (median of 3)",
					small, message_iteration_count);
				co_await report("WebSocket loopback, 64 KiB (median of 3)",
					large, large_message_iteration_count);
				const auto closed = co_await stream.close(riwo::use_awaitable);
				RIWO_TEST_CHECK(closed.clean);
			}
			catch(...)
			{
				service.stop();
				throw;
			}
			service.stop();
			co_return;
		}, asio::use_future);

	context.run();
	completed.get();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"WebSocket frame codec throughput", frame_codec_throughput},
		{"WebSocket message loopback throughput", message_loopback_throughput},
	});
}
