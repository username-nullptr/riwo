// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/utils/signal_slot.h>

namespace
{

#ifdef NDEBUG
constexpr size_t emission_count = 5'000'000 * riwo::test::performance_scale;
#else
constexpr size_t emission_count = 100'000 * riwo::test::performance_scale;
#endif

constexpr size_t asynchronous_emission_count = emission_count / 10;
constexpr size_t connection_cycle_count = emission_count / 10;
#ifdef NDEBUG
constexpr size_t large_value_emission_count =
	512 * riwo::test::performance_scale;
constexpr size_t large_shared_emission_count =
	100'000 * riwo::test::performance_scale;
#else
constexpr size_t large_value_emission_count =
	64 * riwo::test::performance_scale;
constexpr size_t large_shared_emission_count =
	5'000 * riwo::test::performance_scale;
#endif
constexpr size_t large_payload_size = 1'024 * 1'024;

void connection_slot(size_t) {}

void measure_synchronous_signal(size_t slot_count, std::string_view name)
{
	riwo::utils::signal<void(size_t)> fired;
	std::uint64_t checksum = 0;
	std::array<std::function<void(size_t)>,8> slots;
	for(size_t index = 0; index < slot_count; ++index)
		slots[index] = [&](size_t value) { checksum += value; };
	if(slot_count == 1)
		fired.connect<riwo::utils::slot_mode::sync>(std::move(slots[0]));
	else
	{
		fired.connect<riwo::utils::slot_mode::sync>(
			std::move(slots[0]), std::move(slots[1]),
			std::move(slots[2]), std::move(slots[3]),
			std::move(slots[4]), std::move(slots[5]),
			std::move(slots[6]), std::move(slots[7])
		);
	}

	for(size_t index = 0; index < 1'000; ++index)
		fired(index);
	checksum = 0;

	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 1; index <= emission_count; ++index)
		fired(index);
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	const auto expected = static_cast<std::uint64_t>(emission_count) *
		(static_cast<std::uint64_t>(emission_count) + 1) / 2 * slot_count;
	RIWO_TEST_CHECK_EQ(checksum, expected);
	riwo::test::print_performance_result(
		name, emission_count, elapsed, "emit"
	);
}

void synchronous_signal_throughput()
{
	measure_synchronous_signal(1, "signal-slot/synchronous 1 slot");
	measure_synchronous_signal(8, "signal-slot/synchronous 8 slots");
}

void asynchronous_signal_throughput()
{
	riwo::io_context_t context;
	riwo::utils::signal<void(size_t)> fired;
	std::uint64_t checksum = 0;
	fired.connect<riwo::utils::slot_mode::async>(
		context, [&](size_t value) { checksum += value; }
	);

	for(size_t index = 0; index < 1'000; ++index)
		fired(index);
	context.run();
	context.restart();
	checksum = 0;

	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 1; index <= asynchronous_emission_count; ++index)
		fired(index);
	context.run();
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	const auto expected = static_cast<std::uint64_t>(asynchronous_emission_count) *
		(static_cast<std::uint64_t>(asynchronous_emission_count) + 1) / 2;
	RIWO_TEST_CHECK_EQ(checksum, expected);
	riwo::test::print_performance_result(
		"signal-slot/asynchronous 1 slot", asynchronous_emission_count,
		elapsed, "event"
	);
}

void connection_throughput()
{
	riwo::utils::signal<void(size_t)> fired;
	for(size_t index = 0; index < 100; ++index)
	{
		fired.connect<riwo::utils::slot_mode::sync>(connection_slot);
		fired.disconnect(connection_slot);
	}

	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < connection_cycle_count; ++index)
	{
		fired.connect<riwo::utils::slot_mode::sync>(connection_slot);
		fired.disconnect(connection_slot);
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	riwo::test::print_performance_result(
		"signal-slot/connect + disconnect", connection_cycle_count,
		elapsed, "cycle"
	);
}

void print_large_payload_result(
	std::string_view name, size_t count, size_t slot_count,
	std::chrono::steady_clock::duration elapsed)
{
	riwo::test::print_performance_result(name, count, elapsed, "emit");
	const auto bytes = static_cast<double>(count) *
		static_cast<double>(slot_count) * large_payload_size;
	const auto seconds = std::chrono::duration<double>(elapsed).count();
	std::cout << "[PERF] " << name << " logical payload: "
		<< bytes / seconds / (1'024.0 * 1'024.0) << " MiB/s\n";
}

void large_synchronous_arguments()
{
	std::vector<std::byte> payload(large_payload_size, std::byte {0x2a});
	riwo::utils::signal<void(std::vector<std::byte>)> borrowed;
	size_t checksum = 0;
	borrowed.connect<riwo::utils::slot_mode::sync>(
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); }
	);

	const auto borrowed_begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < large_shared_emission_count; ++index)
		borrowed(payload);
	const auto borrowed_elapsed = std::chrono::steady_clock::now() - borrowed_begin;
	RIWO_TEST_CHECK_EQ(
		checksum, large_shared_emission_count * large_payload_size * 4
	);
	print_large_payload_result(
		"signal-slot/synchronous const-ref 4 slots (1 MiB)",
		large_shared_emission_count, 4, borrowed_elapsed
	);

	riwo::utils::signal<void(std::vector<std::byte>)> by_value;
	checksum = 0;
	by_value.connect<riwo::utils::slot_mode::sync>(
		[&](std::vector<std::byte> value) { checksum += value.size(); }
	);
	const auto value_begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < large_value_emission_count; ++index)
		by_value(payload);
	const auto value_elapsed = std::chrono::steady_clock::now() - value_begin;
	RIWO_TEST_CHECK_EQ(checksum, large_value_emission_count * large_payload_size);
	print_large_payload_result(
		"signal-slot/synchronous by-value 1 slot (1 MiB)",
		large_value_emission_count, 1, value_elapsed
	);
}

void large_asynchronous_shared_arguments()
{
	riwo::io_context_t context;
	using payload_t = std::vector<std::byte>;
	using shared_payload_t = std::shared_ptr<const payload_t>;
	riwo::utils::signal<void(shared_payload_t)> fired;
	auto payload = std::make_shared<const payload_t>(
		large_payload_size, std::byte {0x2a}
	);
	size_t checksum = 0;
	fired.connect<riwo::utils::slot_mode::async>(
		context, [&](shared_payload_t value) { checksum += value->size(); }
	);

	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < large_shared_emission_count; ++index)
		fired(payload);
	context.run();
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum, large_shared_emission_count * large_payload_size);
	print_large_payload_result(
		"signal-slot/asynchronous shared immutable 1 slot (1 MiB)",
		large_shared_emission_count, 1, elapsed
	);
}

void large_awaitable_arguments()
{
	riwo::io_context_t context;
	std::vector<std::byte> payload(large_payload_size, std::byte {0x2a});
	riwo::utils::signal<riwo::awaitable<void>(std::vector<std::byte>)> fired;
	size_t checksum = 0;
	fired.connect<riwo::utils::slot_mode::sync>(
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); },
		[&](const std::vector<std::byte> &value) { checksum += value.size(); }
	);

	auto future = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		for(size_t index = 0; index < large_value_emission_count; ++index)
			co_await fired(payload);
		co_return ;
	}, riwo::use_future);
	const auto begin = std::chrono::steady_clock::now();
	context.run();
	future.get();
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(
		checksum, large_value_emission_count * large_payload_size * 4
	);
	print_large_payload_result(
		"signal-slot/awaitable const-ref 4 slots (1 MiB)",
		large_value_emission_count, 4, elapsed
	);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"synchronous signal throughput", synchronous_signal_throughput},
		{"asynchronous signal throughput", asynchronous_signal_throughput},
		{"connection throughput", connection_throughput},
		{"large synchronous signal arguments", large_synchronous_arguments},
		{"large asynchronous shared signal arguments", large_asynchronous_shared_arguments},
		{"large awaitable signal arguments", large_awaitable_arguments},
	});
}
