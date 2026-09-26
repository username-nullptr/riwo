// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/sbus.h>

#include <cstring>
#include <future>

namespace
{

using namespace std::chrono_literals;
using cache_t = riwo::utils::sbus::local_cache;

constexpr std::string_view raw_topic = "riwo.test.sbus.cache.raw";
constexpr std::string_view vector_topic = "riwo.test.sbus.cache.vector";
constexpr std::string_view text_topic = "riwo.test.sbus.cache.text";
constexpr std::string_view batch_topic = "riwo.test.sbus.cache.batch";
constexpr std::string_view signal_topic = "riwo.test.sbus.cache.signal";
constexpr std::string_view published_topic = "riwo.test.sbus.cache.published";
constexpr std::string_view sync_wait_topic = "riwo.test.sbus.cache.wait.sync";
constexpr std::string_view error_wait_topic = "riwo.test.sbus.cache.wait.error";
constexpr std::string_view future_wait_topic = "riwo.test.sbus.cache.wait.future";
constexpr std::string_view callback_wait_topic = "riwo.test.sbus.cache.wait.callback";
constexpr std::string_view edge_wait_topic = "riwo.test.sbus.cache.wait.edge";
constexpr std::string_view deferred_wait_topic = "riwo.test.sbus.cache.wait.deferred";
constexpr std::string_view awaitable_wait_topic = "riwo.test.sbus.cache.wait.awaitable";
constexpr std::string_view timeout_wait_topic = "riwo.test.sbus.cache.wait.timeout";
constexpr std::string_view cancel_wait_topic = "riwo.test.sbus.cache.wait.cancel";
constexpr std::string_view object_cancel_wait_topic =
	"riwo.test.sbus.cache.wait.object-cancel";
constexpr std::string_view sync_cancel_wait_topic =
	"riwo.test.sbus.cache.wait.sync-cancel";

struct plain_topic
{
	RIWO_UTILS_SBUS_TYPE_IMPL("riwo.test.sbus.cache.typed-plain")
	std::uint32_t value = 0;
};

struct structured_topic
{
	RIWO_UTILS_SBUS_META_TYPE (
		cache_structured,
		( std::uint32_t, sequence ),
		( std::string , message  )
	)
};

static_assert(riwo::utils::sbus::concepts::topic_type<plain_topic>);
static_assert(riwo::utils::sbus::concepts::topic_type<structured_topic>);
static_assert(not cache_t::is_token_v<const riwo::detached_t&,std::uint32_t>);

bool wait_for_count(const std::atomic_size_t &count, size_t expected)
{
	for(int retry = 0; retry < 2'000 and count.load() < expected; ++retry)
		std::this_thread::sleep_for(1ms);
	return count.load() == expected;
}

void drain_pool(asio::thread_pool &pool)
{
	auto completed = std::make_shared<std::promise<void>>();
	auto future = completed->get_future();
	asio::post(pool, [&pool, completed] {
		asio::post(pool, [completed] { completed->set_value(); });
	});
	RIWO_TEST_CHECK(future.wait_for(2s) == std::future_status::ready);
	future.get();
}

template <typename T>
T future_result(std::future<T> &future)
{
	RIWO_TEST_CHECK(future.wait_for(2s) == std::future_status::ready);
	return future.get();
}

void set_get_and_snapshot()
{
	asio::thread_pool pool(1);
	cache_t cache(pool);

	RIWO_TEST_CHECK(not cache.get<std::uint32_t>(raw_topic));
	RIWO_TEST_CHECK(cache.get(raw_topic).empty());
	RIWO_TEST_CHECK(cache.get().empty());
	RIWO_TEST_CHECK(cache.subscriber().interface() != nullptr);

	const std::uint32_t raw_value = 0x12345678U;
	cache.set(raw_topic, &raw_value, sizeof(raw_value));
	RIWO_TEST_CHECK_EQ(cache.get<std::uint32_t>(raw_topic).value_or(0), raw_value);
	const auto raw_payload = cache.get(raw_topic);
	RIWO_TEST_CHECK_EQ(raw_payload.size(), sizeof(raw_value));
	RIWO_TEST_CHECK(std::memcmp(raw_payload.data(), &raw_value, sizeof(raw_value)) == 0);

	const std::vector<std::int32_t> values {3, 1, 4, 1, 5};
	cache.set(vector_topic, values);
	RIWO_TEST_CHECK_EQ(cache.get<std::vector<std::int32_t>>(vector_topic),
		riwo::optional(values));

	cache.set(text_topic, std::string_view("cache-text"));
	const auto text_payload = cache.get(text_topic);
	const std::string text(
		reinterpret_cast<const char*>(text_payload.data()), text_payload.size()
	);
	RIWO_TEST_CHECK_EQ(text, "cache-text");

	cache.set(batch_topic, std::uint32_t {7}, std::uint32_t {9});
	RIWO_TEST_CHECK_EQ(cache.get<std::uint32_t>(batch_topic).value_or(0), 9U);

	const plain_topic plain {0xaabbccddU};
	cache.set(plain);
	const auto restored_plain = cache.get<plain_topic>();
	RIWO_TEST_CHECK(restored_plain.has_value());
	RIWO_TEST_CHECK_EQ(restored_plain->value, plain.value);

	const structured_topic structured {
		.sequence = 42,
		.message = "structured payload",
	};
	cache.set(structured);
	const auto restored_structured = cache.get<structured_topic>();
	RIWO_TEST_CHECK(restored_structured.has_value());
	RIWO_TEST_CHECK_EQ(restored_structured->sequence, structured.sequence);
	RIWO_TEST_CHECK_EQ(restored_structured->message, structured.message);
	RIWO_TEST_CHECK_THROWS(
		cache.get<structured_topic>("riwo.test.sbus.cache.wrong-topic"),
		riwo::invalid_argument
	);

	const auto snapshot = cache.get();
	RIWO_TEST_CHECK_EQ(snapshot.at(std::string(raw_topic)), raw_payload);
	RIWO_TEST_CHECK_EQ(snapshot.at(std::string(text_topic)), text_payload);
	RIWO_TEST_CHECK(snapshot.contains(std::string(plain_topic::riwo_sbus_topic_v)));
	RIWO_TEST_CHECK(snapshot.contains(std::string(structured_topic::riwo_sbus_topic_v)));

	cache.subscriber().cancel();
	pool.stop();
	pool.join();
}

void change_signals_and_duplicate_suppression()
{
	asio::thread_pool pool(1);
	cache_t cache(pool);
	std::atomic_size_t topic_changes {0};
	std::atomic_size_t global_changes {0};
	std::atomic_uint32_t topic_current {0};
	std::atomic_uint32_t topic_previous {0};
	std::atomic_uint32_t global_current {0};
	std::atomic_uint32_t global_previous {0};
	std::atomic_bool wrong_global_topic {false};

	cache.changed(signal_topic).connect(
	[&](std::uint32_t current, std::uint32_t previous)
	{
		topic_current.store(current, std::memory_order_relaxed);
		topic_previous.store(previous, std::memory_order_relaxed);
		topic_changes.fetch_add(1, std::memory_order_release);
	});
	cache.changed().connect(
	[&](std::string_view topic, std::uint32_t current, std::uint32_t previous)
	{
		if( topic != signal_topic )
			wrong_global_topic.store(true, std::memory_order_relaxed);
		global_current.store(current, std::memory_order_relaxed);
		global_previous.store(previous, std::memory_order_relaxed);
		global_changes.fetch_add(1, std::memory_order_release);
	});

	cache.set(signal_topic, std::uint32_t {11});
	RIWO_TEST_CHECK(wait_for_count(topic_changes, 1));
	RIWO_TEST_CHECK(wait_for_count(global_changes, 1));
	RIWO_TEST_CHECK_EQ(topic_current.load(), 11U);
	RIWO_TEST_CHECK_EQ(topic_previous.load(), 0U);
	RIWO_TEST_CHECK_EQ(global_current.load(), 11U);
	RIWO_TEST_CHECK_EQ(global_previous.load(), 0U);
	RIWO_TEST_CHECK(not wrong_global_topic.load());

	cache.set(signal_topic, std::uint32_t {11});
	drain_pool(pool);
	RIWO_TEST_CHECK_EQ(topic_changes.load(), 1U);
	RIWO_TEST_CHECK_EQ(global_changes.load(), 1U);

	cache.set(signal_topic, std::uint32_t {22});
	RIWO_TEST_CHECK(wait_for_count(topic_changes, 2));
	RIWO_TEST_CHECK(wait_for_count(global_changes, 2));
	RIWO_TEST_CHECK_EQ(topic_current.load(), 22U);
	RIWO_TEST_CHECK_EQ(topic_previous.load(), 11U);
	RIWO_TEST_CHECK_EQ(global_current.load(), 22U);
	RIWO_TEST_CHECK_EQ(global_previous.load(), 11U);

	cache.changed().disconnect();
	std::atomic_size_t typed_changes {0};
	std::atomic_uint32_t typed_current {0};
	std::atomic_uint32_t typed_previous {0};
	cache.changed<structured_topic>().connect(
	[&](structured_topic current, structured_topic previous)
	{
		typed_current.store(current.sequence, std::memory_order_relaxed);
		typed_previous.store(previous.sequence, std::memory_order_relaxed);
		typed_changes.fetch_add(1, std::memory_order_release);
	});
	cache.set(structured_topic {.sequence = 31, .message = "first"});
	RIWO_TEST_CHECK(wait_for_count(typed_changes, 1));
	cache.set(structured_topic {.sequence = 47, .message = "second"});
	RIWO_TEST_CHECK(wait_for_count(typed_changes, 2));
	RIWO_TEST_CHECK_EQ(typed_current.load(), 47U);
	RIWO_TEST_CHECK_EQ(typed_previous.load(), 31U);

	cache.changed(signal_topic).disconnect();
	cache.changed<structured_topic>().disconnect();
	cache.subscriber().cancel();
	pool.stop();
	pool.join();
}

void published_values_populate_cache()
{
	asio::thread_pool pool(1);
	cache_t cache(pool);
	std::atomic_size_t changes {0};
	cache.changed(published_topic).connect(
	[&](std::uint32_t, std::uint32_t) {
		changes.fetch_add(1, std::memory_order_release);
	});

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		published_topic, std::uint32_t {101}
	);
	RIWO_TEST_CHECK(wait_for_count(changes, 1));
	RIWO_TEST_CHECK_EQ(cache.get<std::uint32_t>(published_topic).value_or(0), 101U);

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		published_topic, std::uint32_t {101}
	);
	drain_pool(pool);
	RIWO_TEST_CHECK_EQ(changes.load(), 1U);

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		published_topic, std::uint32_t {202}
	);
	RIWO_TEST_CHECK(wait_for_count(changes, 2));
	RIWO_TEST_CHECK_EQ(cache.get<std::uint32_t>(published_topic).value_or(0), 202U);

	const structured_topic structured {
		.sequence = 73,
		.message = "published structured payload",
	};
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(structured);
	for(int retry = 0; retry < 2'000 and not cache.get<structured_topic>(); ++retry)
		std::this_thread::sleep_for(1ms);
	const auto restored = cache.get<structured_topic>();
	RIWO_TEST_CHECK(restored.has_value());
	RIWO_TEST_CHECK_EQ(restored->sequence, structured.sequence);
	RIWO_TEST_CHECK_EQ(restored->message, structured.message);

	cache.changed(published_topic).disconnect();
	cache.subscriber().cancel();
	pool.stop();
	pool.join();
}

void wait_changed_tokens()
{
	using changed_t = cache_t::changed_result<std::uint32_t>;
	asio::thread_pool pool(1);
	cache_t cache(pool);

	static_assert(std::same_as<
		decltype(cache.wait_changed<std::uint32_t>(future_wait_topic, riwo::use_future)),
		std::future<changed_t>
	>);

	riwo::jthread sync_setter([&] {
		std::this_thread::sleep_for(10ms);
		cache.set(sync_wait_topic, std::uint32_t {17});
	});
	const auto sync_result = cache.wait_changed<std::uint32_t>(sync_wait_topic);
	RIWO_TEST_CHECK_EQ(sync_result.current, 17U);
	RIWO_TEST_CHECK_EQ(sync_result.previous, 0U);
	riwo::error_code sync_error =
		asio::error::make_error_code(asio::error::operation_aborted);
	riwo::jthread error_setter([&] {
		std::this_thread::sleep_for(10ms);
		cache.set(error_wait_topic, std::uint32_t {19});
	});
	const auto error_result =
		cache.wait_changed<std::uint32_t>(error_wait_topic, sync_error);
	RIWO_TEST_CHECK(not sync_error);
	RIWO_TEST_CHECK_EQ(error_result.current, 19U);
	RIWO_TEST_CHECK_EQ(error_result.previous, 0U);

	auto future = cache.wait_changed<std::uint32_t>(future_wait_topic, riwo::use_future);
	drain_pool(pool);
	cache.set(future_wait_topic, std::uint32_t {23});
	const auto future_change = future_result(future);
	RIWO_TEST_CHECK_EQ(future_change.current, 23U);
	RIWO_TEST_CHECK_EQ(future_change.previous, 0U);

	auto callback_promise = std::make_shared<std::promise<changed_t>>();
	auto callback_future = callback_promise->get_future();
	cache.wait_changed<std::uint32_t>(callback_wait_topic,
		[callback_promise](riwo::error_code error, changed_t result) mutable {
			RIWO_TEST_CHECK(not error);
			callback_promise->set_value(std::move(result));
		});
	drain_pool(pool);
	cache.set(callback_wait_topic, std::uint32_t {29});
	const auto callback_change = future_result(callback_future);
	RIWO_TEST_CHECK_EQ(callback_change.current, 29U);
	RIWO_TEST_CHECK_EQ(callback_change.previous, 0U);

	cache.set(edge_wait_topic, std::uint32_t {31});
	drain_pool(pool);
	auto edge_future = cache.wait_changed<std::uint32_t>(
		edge_wait_topic, riwo::use_future
	);
	RIWO_TEST_CHECK(edge_future.wait_for(0ms) == std::future_status::timeout);
	cache.set(edge_wait_topic, std::uint32_t {31});
	drain_pool(pool);
	RIWO_TEST_CHECK(edge_future.wait_for(0ms) == std::future_status::timeout);
	cache.set(edge_wait_topic, std::uint32_t {37});
	const auto edge_change = future_result(edge_future);
	RIWO_TEST_CHECK_EQ(edge_change.current, 37U);
	RIWO_TEST_CHECK_EQ(edge_change.previous, 31U);

	auto deferred_wait = cache.wait_changed<std::uint32_t>(
		deferred_wait_topic, riwo::deferred
	);
	cache.set(deferred_wait_topic, std::uint32_t {41});
	drain_pool(pool);
	auto deferred_future = std::move(deferred_wait)(riwo::use_future);
	RIWO_TEST_CHECK(deferred_future.wait_for(0ms) == std::future_status::timeout);
	cache.set(deferred_wait_topic, std::uint32_t {43});
	const auto deferred_change = future_result(deferred_future);
	RIWO_TEST_CHECK_EQ(deferred_change.current, 43U);
	RIWO_TEST_CHECK_EQ(deferred_change.previous, 41U);

	auto awaitable_future = asio::co_spawn(pool,
		[&cache]() -> riwo::awaitable<changed_t>
		{
			co_return co_await cache.wait_changed<std::uint32_t>(
				awaitable_wait_topic, riwo::use_awaitable
			);
		}, asio::use_future);
	drain_pool(pool);
	cache.set(awaitable_wait_topic, std::uint32_t {47});
	const auto awaitable_change = future_result(awaitable_future);
	RIWO_TEST_CHECK_EQ(awaitable_change.current, 47U);
	RIWO_TEST_CHECK_EQ(awaitable_change.previous, 0U);

	riwo::error_code timeout_error;
	auto timed_future = cache.wait_changed<std::uint32_t>(timeout_wait_topic,
		riwo::redirect_time(
			asio::redirect_error(riwo::use_future, timeout_error), 20ms
		));
	(void) future_result(timed_future);
	RIWO_TEST_CHECK_EQ(timeout_error,
		asio::error::make_error_code(asio::error::timed_out));

	asio::cancellation_signal cancellation;
	riwo::error_code cancellation_error;
	auto cancelled_future = cache.wait_changed<std::uint32_t>(cancel_wait_topic,
		asio::bind_cancellation_slot(cancellation.slot(),
			asio::redirect_error(riwo::use_future, cancellation_error)
		));
	cancellation.emit(asio::cancellation_type::terminal);
	(void) future_result(cancelled_future);
	RIWO_TEST_CHECK_EQ(cancellation_error,
		asio::error::make_error_code(asio::error::operation_aborted));

	std::atomic_size_t persistent_changes {0};
	cache.changed(object_cancel_wait_topic).connect(
	[&persistent_changes](std::uint32_t, std::uint32_t) {
		persistent_changes.fetch_add(1, std::memory_order_release);
	});
	riwo::error_code object_cancel_error;
	auto object_cancelled_future = cache.wait_changed<std::uint32_t>(
		object_cancel_wait_topic,
		asio::redirect_error(riwo::use_future, object_cancel_error)
	);
	cache.cancel();
	(void) future_result(object_cancelled_future);
	RIWO_TEST_CHECK_EQ(object_cancel_error,
		asio::error::make_error_code(asio::error::operation_aborted));

	auto reusable_future = cache.wait_changed<std::uint32_t>(
		object_cancel_wait_topic, riwo::use_future
	);
	cache.set(object_cancel_wait_topic, std::uint32_t {53});
	const auto reusable_change = future_result(reusable_future);
	RIWO_TEST_CHECK_EQ(reusable_change.current, 53U);
	RIWO_TEST_CHECK_EQ(reusable_change.previous, 0U);
	RIWO_TEST_CHECK(wait_for_count(persistent_changes, 1));
	cache.changed(object_cancel_wait_topic).disconnect();

	riwo::error_code sync_cancel_error;
	changed_t sync_cancel_result {};
	std::atomic_bool sync_cancel_done {false};
	riwo::jthread sync_cancel_waiter([&] {
		sync_cancel_result = cache.wait_changed<std::uint32_t>(
			sync_cancel_wait_topic, sync_cancel_error
		);
		sync_cancel_done.store(true, std::memory_order_release);
	});
	for(int retry = 0; retry < 2'000 and
		not sync_cancel_done.load(std::memory_order_acquire); ++retry)
	{
		cache.cancel();
		std::this_thread::sleep_for(1ms);
	}
	RIWO_TEST_CHECK(sync_cancel_done.load(std::memory_order_acquire));
	sync_cancel_waiter.join();
	RIWO_TEST_CHECK_EQ(sync_cancel_error,
		asio::error::make_error_code(asio::error::operation_aborted));
	RIWO_TEST_CHECK_EQ(sync_cancel_result.current, 0U);
	RIWO_TEST_CHECK_EQ(sync_cancel_result.previous, 0U);

	const structured_topic structured {
		.sequence = 97,
		.message = "typed wait",
	};
	auto typed_future = cache.wait_changed<structured_topic>(riwo::use_future);
	drain_pool(pool);
	cache.set(structured);
	const auto typed_result = future_result(typed_future);
	RIWO_TEST_CHECK_EQ(typed_result.current.sequence, structured.sequence);
	RIWO_TEST_CHECK_EQ(typed_result.current.message, structured.message);
	RIWO_TEST_CHECK_EQ(typed_result.previous.sequence, 0U);
	RIWO_TEST_CHECK(typed_result.previous.message.empty());

	using structured_changed_t = cache_t::changed_result<structured_topic>;
	auto typed_promise = std::make_shared<std::promise<structured_changed_t>>();
	auto typed_callback_future = typed_promise->get_future();
	cache.wait_changed<structured_topic>(
		[typed_promise](riwo::error_code error, structured_changed_t result) mutable {
			RIWO_TEST_CHECK(not error);
			typed_promise->set_value(std::move(result));
		});
	drain_pool(pool);
	const structured_topic updated_structured {
		.sequence = 101,
		.message = "typed callback wait",
	};
	cache.set(updated_structured);
	const auto typed_callback_result = future_result(typed_callback_future);
	RIWO_TEST_CHECK_EQ(typed_callback_result.current.sequence,
		updated_structured.sequence);
	RIWO_TEST_CHECK_EQ(typed_callback_result.current.message,
		updated_structured.message);
	RIWO_TEST_CHECK_EQ(typed_callback_result.previous.sequence,
		structured.sequence);
	RIWO_TEST_CHECK_EQ(typed_callback_result.previous.message,
		structured.message);
	RIWO_TEST_CHECK_THROWS(
		cache.wait_changed<structured_topic>(
			"riwo.test.sbus.cache.wrong-topic", riwo::use_future),
		riwo::invalid_argument
	);

	cache.subscriber().cancel();
	pool.stop();
	pool.join();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"sbus cache set, get, and snapshot", set_get_and_snapshot},
		{"sbus cache change signals and duplicate suppression",
			change_signals_and_duplicate_suppression},
		{"sbus published values populate cache", published_values_populate_cache},
		{"sbus cache wait_changed tokens", wait_changed_tokens},
	});
}
