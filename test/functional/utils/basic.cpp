// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/observer.h>
#include <riwo/utils/signal_slot.h>

namespace
{

using namespace std::chrono_literals;

static_assert(riwo::test::canonical_executor_type<
	riwo::utils::observer<void()>>);

struct awaitable_sender
{
	riwo::utils::signal<riwo::awaitable<void>(std::string)> fired;
};

struct async_sender
{
	riwo::utils::signal<void(std::string)> fired;
};

struct awaitable_observer
{
	std::shared_ptr<awaitable_observer> *owner = nullptr;
	bool *destroyed = nullptr;
	int *received = nullptr;

	~awaitable_observer()
	{
		*destroyed = true;
	}

	riwo::awaitable<void> receive(int value)
	{
		auto exec = co_await asio::this_coro::executor;
		auto *shared_owner = owner;
		asio::post(exec, [shared_owner] { shared_owner->reset(); });

		asio::steady_timer timer(exec, 1ms);
		co_await timer.async_wait(asio::use_awaitable);
		*received = *destroyed ? -1 : value;
	}
};

struct synchronous_observer
{
	int *received = nullptr;

	void receive(int value)
	{
		*received = value;
	}
};

riwo::utils::signal<void()> *reentrant_signal = nullptr;
int reentrant_first_count = 0;
int reentrant_second_count = 0;
std::atomic<std::int64_t> *concurrent_received = nullptr;

struct tracked_payload
{
	tracked_payload() : bytes(64 * 1'024, std::byte {0x2a}) {}

	tracked_payload(const tracked_payload &other) : bytes(other.bytes) {
		copies.fetch_add(1, std::memory_order_relaxed);
	}
	tracked_payload(tracked_payload&&) noexcept = default;
	tracked_payload &operator=(const tracked_payload&) = default;
	tracked_payload &operator=(tracked_payload&&) noexcept = default;

	static inline std::atomic_size_t copies {0};
	std::vector<std::byte> bytes;
};

void reentrant_second()
{
	++reentrant_second_count;
}

void reentrant_first()
{
	++reentrant_first_count;
	reentrant_signal->disconnect(reentrant_first);
	reentrant_signal->connect(reentrant_second);
}

void concurrent_slot(int value)
{
	concurrent_received->fetch_add(value, std::memory_order_relaxed);
}

void synchronous_signal()
{
	riwo::utils::signal<void(int,std::string_view)> changed;
	int total = 0;
	std::string_view last_label;

	changed.connect([&](int value, std::string_view label) {
		total += value;
		last_label = label;
	});
	changed(3, "first");
	RIWO_TEST_CHECK_EQ(total, 3);
	RIWO_TEST_CHECK_EQ(last_label, "first");

	changed.block();
	RIWO_TEST_CHECK(changed.is_blocked());
	changed(10, "blocked");
	RIWO_TEST_CHECK_EQ(total, 3);

	changed.block(false);
	changed.disconnect();
	changed(10, "disconnected");
	RIWO_TEST_CHECK_EQ(total, 3);
}

void backpressure_signal()
{
	asio::thread_pool pool(1);
	riwo::utils::signal<void(int)> fired;
	int received = 0;
	fired.connect<riwo::utils::slot_mode::backpressure>(
		pool, [&](int value) { received = value; }
	);

	fired(42);
	RIWO_TEST_CHECK_EQ(received, 42);
	pool.join();
}

void observer_lifecycle()
{
	using observer_t = riwo::utils::basic_observer<
		riwo::io_executor_t, void(int)
	>;

	riwo::io_context_t context;
	int received = 0;
	auto observer = observer_t::make(7, context.get_executor());
	observer->on_triggered<0>([&](int value) {
		received += value;
	});

	observer_t::trigger<0>(7, 4);
	context.run();
	RIWO_TEST_CHECK_EQ(received, 4);

	observer.reset();
	context.restart();
	observer_t::trigger<0>(7, 8);
	context.run();
	RIWO_TEST_CHECK_EQ(received, 4);
}

void awaitable_signal_lifecycle()
{
	riwo::io_context_t context;
	std::string received;
	auto result = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto sender = std::make_unique<awaitable_sender>();
		sender->fired.connect<riwo::utils::slot_mode::sync>(
			[&](std::string value) -> riwo::awaitable<void>
			{
				auto exec = co_await asio::this_coro::executor;
				asio::steady_timer timer(exec, 1ms);
				co_await timer.async_wait(asio::use_awaitable);
				received = std::move(value);
			}
		);

		auto pending = sender->fired.emit(std::string(4096, 'x'));
		sender.reset();
		co_await std::move(pending);
	}, asio::use_future);

	context.run();
	result.get();
	RIWO_TEST_CHECK_EQ(received, std::string(4096, 'x'));
}

void async_signal_lifecycle()
{
	riwo::io_context_t context;
	std::string received;
	auto sender = std::make_unique<async_sender>();
	sender->fired.connect<riwo::utils::slot_mode::async>(
		context, [&](std::string value) { received = std::move(value); }
	);

	sender->fired.emit(std::string(4096, 'y'));
	sender.reset();
	context.run();
	RIWO_TEST_CHECK_EQ(received, std::string(4096, 'y'));
}

void signal_observer_lifecycle()
{
	riwo::io_context_t context;
	riwo::utils::signal<void(int)> fired;
	bool destroyed = false;
	int received = 0;

	auto observer = std::make_shared<awaitable_observer>();
	observer->owner = &observer;
	observer->destroyed = &destroyed;
	observer->received = &received;
	fired.connect<riwo::utils::slot_mode::async>(
		observer, context, &awaitable_observer::receive
	);

	fired.emit(9);
	context.run();
	RIWO_TEST_CHECK(not observer);
	RIWO_TEST_CHECK(destroyed);
	RIWO_TEST_CHECK_EQ(received, 9);
}

void expired_signal_observer()
{
	riwo::io_context_t context;
	riwo::utils::signal<void(int)> fired;
	int received = 0;
	auto observer = std::make_shared<synchronous_observer>();
	observer->received = &received;
	fired.connect<riwo::utils::slot_mode::async>(
		observer, context, &synchronous_observer::receive
	);

	fired.emit(9);
	observer.reset();
	context.run();
	RIWO_TEST_CHECK_EQ(received, 0);
}

void invalid_signal_observer()
{
	riwo::utils::signal<void(int)> fired;
	std::shared_ptr<synchronous_observer> observer;
	RIWO_TEST_CHECK_THROWS((fired.connect<riwo::utils::slot_mode::sync>(
		observer, &synchronous_observer::receive)), riwo::invalid_argument);
	RIWO_TEST_CHECK_THROWS((fired.disconnect(
		observer, &synchronous_observer::receive)), riwo::invalid_argument);
	RIWO_TEST_CHECK_THROWS(fired.disconnect(observer), riwo::invalid_argument);
}

void repeated_signal_operations()
{
	riwo::utils::signal<void(int)> fired;
	std::int64_t received = 0;
	for(int round = 1; round <= 10'000; ++round)
	{
		fired.connect([&](int value) { received += value; });
		fired(round);

		fired.block();
		fired(round);
		fired.block(false);

		fired.disconnect();
		fired.disconnect();
		fired(round);
	}

	RIWO_TEST_CHECK_EQ(received, 50'005'000);
}

void disconnected_slot_releases_resources()
{
	riwo::utils::signal<void()> fired;
	auto resource = std::make_shared<int>(42);
	std::weak_ptr<int> weak_resource = resource;
	fired.connect([resource] {});
	resource.reset();
	RIWO_TEST_CHECK(not weak_resource.expired());

	fired.disconnect();
	RIWO_TEST_CHECK(weak_resource.expired());
}

void reentrant_signal_mutation()
{
	riwo::utils::signal<void()> fired;
	reentrant_signal = &fired;
	reentrant_first_count = 0;
	reentrant_second_count = 0;

	fired.connect(reentrant_first);
	fired();
	RIWO_TEST_CHECK_EQ(reentrant_first_count, 1);
	RIWO_TEST_CHECK_EQ(reentrant_second_count, 0);

	fired();
	RIWO_TEST_CHECK_EQ(reentrant_first_count, 1);
	RIWO_TEST_CHECK_EQ(reentrant_second_count, 1);
	reentrant_signal = nullptr;
}

void concurrent_signal_mutation()
{
	riwo::utils::signal<void(int)> fired;
	std::atomic<std::int64_t> received { 0 };
	std::atomic_bool start { false };
	concurrent_received = &received;
	fired.connect(concurrent_slot);

	std::thread emitter([&]
	{
		while( not start.load(std::memory_order_acquire) )
			std::this_thread::yield();
		for(int index = 0; index < 50'000; ++index)
			fired(1);
	});
	std::thread mutator([&]
	{
		start.store(true, std::memory_order_release);
		for(int index = 0; index < 2'000; ++index)
		{
			fired.disconnect();
			fired.connect(concurrent_slot);
		}
	});

	emitter.join();
	mutator.join();
	fired.disconnect();
	const auto before = received.load(std::memory_order_relaxed);
	fired(1);
	RIWO_TEST_CHECK_EQ(received.load(std::memory_order_relaxed), before);
	concurrent_received = nullptr;
}

void large_synchronous_signal_arguments()
{
	tracked_payload payload;
	riwo::utils::signal<void(tracked_payload)> borrowed;
	size_t received = 0;
	borrowed.connect(
		[&](const tracked_payload &value) { received += value.bytes.size(); },
		[&](const tracked_payload &value) { received += value.bytes.size(); }
	);

	tracked_payload::copies.store(0, std::memory_order_relaxed);
	borrowed(payload);
	RIWO_TEST_CHECK_EQ(tracked_payload::copies.load(std::memory_order_relaxed), 0);
	RIWO_TEST_CHECK_EQ(received, payload.bytes.size() * 2);

	riwo::utils::signal<void(tracked_payload)> by_value;
	by_value.connect(
		[](tracked_payload) {},
		[](tracked_payload) {}
	);
	tracked_payload::copies.store(0, std::memory_order_relaxed);
	by_value(payload);
	RIWO_TEST_CHECK_EQ(tracked_payload::copies.load(std::memory_order_relaxed), 2);
}

void large_awaitable_signal_arguments()
{
	riwo::io_context_t context;
	tracked_payload payload;
	riwo::utils::signal<riwo::awaitable<void>(tracked_payload)> borrowed;
	size_t received = 0;
	borrowed.connect<riwo::utils::slot_mode::sync>(
		[&](const tracked_payload &value) { received += value.bytes.size(); },
		[&](const tracked_payload &value) { received += value.bytes.size(); }
	);

	tracked_payload::copies.store(0, std::memory_order_relaxed);
	auto future = asio::co_spawn(context, borrowed(payload), riwo::use_future);
	context.run();
	future.get();
	RIWO_TEST_CHECK_EQ(tracked_payload::copies.load(std::memory_order_relaxed), 1);
	RIWO_TEST_CHECK_EQ(received, payload.bytes.size() * 2);
}

void large_async_signal_owns_arguments()
{
	riwo::io_context_t context;
	riwo::utils::signal<void(tracked_payload)> fired;
	size_t received_size = 0;
	fired.connect<riwo::utils::slot_mode::async>(
		context, [&](const tracked_payload &value) {
			received_size = value.bytes.size();
		}
	);

	tracked_payload::copies.store(0, std::memory_order_relaxed);
	{
		tracked_payload payload;
		fired(payload);
	}
	context.run();
	RIWO_TEST_CHECK_EQ(tracked_payload::copies.load(std::memory_order_relaxed), 1);
	RIWO_TEST_CHECK_EQ(received_size, 64 * 1'024);
}

void large_any_signal_borrows_stored_value()
{
	riwo::utils::signal<void(std::any)> fired;
	size_t received_size = 0;
	fired.connect([&](const tracked_payload &value) {
		received_size = value.bytes.size();
	});
	std::any payload(std::in_place_type<tracked_payload>);

	tracked_payload::copies.store(0, std::memory_order_relaxed);
	fired(payload);
	RIWO_TEST_CHECK_EQ(tracked_payload::copies.load(std::memory_order_relaxed), 0);
	RIWO_TEST_CHECK_EQ(received_size, 64 * 1'024);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"synchronous signal", synchronous_signal},
		{"backpressure signal", backpressure_signal},
		{"observer lifecycle", observer_lifecycle},
		{"awaitable signal lifecycle", awaitable_signal_lifecycle},
		{"async signal lifecycle", async_signal_lifecycle},
		{"signal observer lifecycle", signal_observer_lifecycle},
		{"expired signal observer", expired_signal_observer},
		{"invalid signal observer", invalid_signal_observer},
		{"repeated signal operations", repeated_signal_operations},
		{"disconnected slot releases resources", disconnected_slot_releases_resources},
		{"reentrant signal mutation", reentrant_signal_mutation},
		{"concurrent signal mutation", concurrent_signal_mutation},
		{"large synchronous signal arguments", large_synchronous_signal_arguments},
		{"large awaitable signal arguments", large_awaitable_signal_arguments},
		{"large async signal owns arguments", large_async_signal_owns_arguments},
		{"large any signal borrows stored value", large_any_signal_borrows_stored_value},
	});
}
