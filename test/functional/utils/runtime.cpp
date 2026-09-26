// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/logger.h>
#include <riwo/utils/process.h>
#include <riwo/utils/sbus.h>
#include <riwo/utils/settings.h>

#include <fstream>
#include <future>

namespace
{

using namespace std::chrono_literals;

static_assert(riwo::test::canonical_executor_type<riwo::utils::process>);

bool wait_for(const std::atomic_bool &state)
{
	for(int retry = 0; retry < 200 and not state; ++retry)
		std::this_thread::sleep_for(1ms);
	return state;
}

template <typename T>
T wait_for(std::future<T> &future)
{
	auto &context = riwo::io_context();
	while( future.wait_for(0ms) != std::future_status::ready )
	{
		context.run_for(1ms);
		context.restart();
	}
	return future.get();
}

void settings_persistence_and_signals()
{
	riwo::test::temporary_directory directory;
	const auto file = directory.path() / "settings.ini";
	auto &settings = riwo::utils::settings::instance("riwo-test-runtime");
	bool loaded = false;
	std::string changed_key;
	riwo::value changed_value;
	settings.loaded.connect([&](const riwo::error_code &error) {
		RIWO_TEST_CHECK(not error);
		loaded = true;
	});
	settings.changed.connect([&](std::string_view key, riwo::value value)
	{
		changed_key = key;
		changed_value = std::move(value);
	});

	RIWO_TEST_CHECK(settings.load_or(file));
	RIWO_TEST_CHECK(loaded);
	settings.set("server/port", 8080);
	RIWO_TEST_CHECK_EQ(changed_key, "server/port");
	RIWO_TEST_CHECK_EQ(changed_value.to_int().value_or(0), 8080);
	RIWO_TEST_CHECK_EQ(settings.get("server/port")->to_int().value_or(0), 8080);
	RIWO_TEST_CHECK(settings.sync());
	RIWO_TEST_CHECK(std::filesystem::is_regular_file(file));
	RIWO_TEST_CHECK_EQ(settings.file_name(), file);

	const auto names = riwo::utils::settings::names();
	RIWO_TEST_CHECK(std::ranges::find(names, "riwo-test-runtime") != names.end());
	settings.changed.disconnect();
	settings.loaded.disconnect();
}

void settings_io_tokens()
{
	riwo::test::temporary_directory directory;
	const auto file = directory.path() / "settings-tokens.ini";
	const auto missing = directory.path() / "missing.ini";
	auto &settings = riwo::utils::settings::instance("riwo-test-settings-tokens");
	static_assert(std::same_as<decltype(settings.load(missing)),riwo::sys_expected<>>);
	static_assert(std::same_as<decltype(settings.sync(file)),riwo::sys_expected<>>);
	static_assert(std::same_as<
		decltype(settings.load(missing, riwo::use_future)),
		std::future<riwo::sys_expected<>>
	>);

	std::atomic_int loaded_count {0};
	std::atomic_int synced_count {0};
	riwo::error_code loaded_error;
	riwo::error_code synced_error;
	settings.loaded.connect([&](riwo::error_code error) {
		loaded_error = error;
		loaded_count++;
	});
	settings.synced.connect([&](riwo::error_code error) {
		synced_error = error;
		synced_count++;
	});

	settings.set("tokens/value", 17);
	auto sync_future = settings.sync(file, riwo::use_future);
	const auto sync_result = wait_for(sync_future);
	RIWO_TEST_CHECK(sync_result);
	RIWO_TEST_CHECK(not synced_error);
	RIWO_TEST_CHECK_EQ(synced_count.load(), 1);

	const auto direct_result = settings.load(missing);
	RIWO_TEST_CHECK(not direct_result);
	RIWO_TEST_CHECK_EQ(direct_result.error(),
		std::make_error_code(std::errc::no_such_file_or_directory));
	riwo::error_code token_error;
	settings.load(missing, token_error);
	RIWO_TEST_CHECK_EQ(token_error, direct_result.error());

	auto load_future = settings.load(missing, riwo::use_future);
	const auto missing_result = wait_for(load_future);
	RIWO_TEST_CHECK(not missing_result);
	RIWO_TEST_CHECK_EQ(missing_result.error(),
		std::make_error_code(std::errc::no_such_file_or_directory));
	RIWO_TEST_CHECK_EQ(loaded_error, missing_result.error());

	auto awaitable_future = asio::co_spawn(riwo::io_context(),
	[&]() -> riwo::awaitable<riwo::sys_expected<>>
	{
		co_return co_await settings.load(missing, riwo::use_awaitable);
	}, riwo::use_future);
	const auto awaitable_result = wait_for(awaitable_future);
	RIWO_TEST_CHECK(not awaitable_result);
	RIWO_TEST_CHECK_EQ(awaitable_result.error(), missing_result.error());

	auto deferred_load = settings.load(file, riwo::deferred);
	auto deferred_future = std::move(deferred_load)(riwo::use_future);
	RIWO_TEST_CHECK(wait_for(deferred_future));

	std::promise<riwo::error_code> callback_result;
	settings.load(file, [&](riwo::error_code error) {
		callback_result.set_value(error);
	});
	auto callback_future = callback_result.get_future();
	RIWO_TEST_CHECK(not wait_for(callback_future));
	RIWO_TEST_CHECK_EQ(settings.get("tokens/value")->to_int().value_or(0), 17);

	riwo::io_context_t completion_context;
	std::promise<riwo::error_code> associated_result;
	auto associated_future = associated_result.get_future();
	std::thread::id completion_thread;
	std::thread::id callback_thread;
	settings.load(file, asio::bind_executor(completion_context.get_executor(),
	[&](riwo::error_code error)
	{
		callback_thread = std::this_thread::get_id();
		associated_result.set_value(error);
	}));
	std::thread completion_runner([&]
	{
		completion_thread = std::this_thread::get_id();
		completion_context.run();
	});
	RIWO_TEST_CHECK(not wait_for(associated_future));
	completion_runner.join();
	RIWO_TEST_CHECK(callback_thread == completion_thread);

	const auto before_detached = loaded_count.load();
	settings.load_or(file, riwo::detached);
	for(int retry = 0; retry < 200 and loaded_count == before_detached; ++retry)
	{
		riwo::io_context().poll();
		riwo::io_context().restart();
		std::this_thread::sleep_for(1ms);
	}
	RIWO_TEST_CHECK(loaded_count > before_detached);
	RIWO_TEST_CHECK(not loaded_error);

	settings.loaded.disconnect();
	settings.synced.disconnect();
}

void settings_error_token_lifetime_stress()
{
	constexpr size_t rounds = 2;
	constexpr size_t token_kinds = 4;
	riwo::test::temporary_directory directory;
	auto &context = riwo::io_context();
	context.restart();

	// Reuse the singleton across --repeat iterations. Creating a fresh settings
	// instance each time would intentionally retain one periodic sync timer per
	// seed and measure singleton accumulation instead of I/O-token lifetimes.
	auto &settings =
		riwo::utils::settings::instance("riwo-test-settings-stress");
	// This case targets completion-token lifetimes. Disable the independent
	// periodic-sync coroutine so cancelled timer completions do not accumulate
	// between --repeat iterations and distort the stress workload.
	settings.ini().set_sync_period(0ms);
	settings.ini().set_sync_on_delete(false);
	const auto expected_error =
		std::make_error_code(std::errc::no_such_file_or_directory);

	std::atomic_size_t loaded_count {0};
	std::atomic_size_t loaded_errors {0};
	settings.loaded.connect([&](riwo::error_code error)
	{
		loaded_count.fetch_add(1, std::memory_order_relaxed);
		if( error == expected_error )
			loaded_errors.fetch_add(1, std::memory_order_relaxed);
	});

	riwo::test::random_sequence sequence;

	for(size_t round = 0; round < rounds; ++round)
	{
		const auto missing = directory.path() /
			("missing-" + std::to_string(round) + ".ini");

		// Complete and destroy every token before starting the next one.  The
		// old stress test queued all operations at once and could turn one slow
		// completion into unbounded retained work across --repeat iterations.
		auto future = settings.load(missing, riwo::use_future);
		const auto future_result = wait_for(future);
		RIWO_TEST_CHECK(not future_result);
		RIWO_TEST_CHECK_EQ(future_result.error(), expected_error);

		auto awaitable = asio::co_spawn(context,
			[&settings, missing]() -> riwo::awaitable<riwo::sys_expected<>>
			{
				co_return co_await settings.load(missing, riwo::use_awaitable);
			}, riwo::use_future);
		const auto awaitable_result = wait_for(awaitable);
		RIWO_TEST_CHECK(not awaitable_result);
		RIWO_TEST_CHECK_EQ(awaitable_result.error(), expected_error);

		auto deferred = settings.load(missing, riwo::deferred);
		auto deferred_future = std::move(deferred)(riwo::use_future);
		const auto deferred_result = wait_for(deferred_future);
		RIWO_TEST_CHECK(not deferred_result);
		RIWO_TEST_CHECK_EQ(deferred_result.error(), expected_error);

		std::promise<riwo::error_code> callback_result;
		auto callback_future = callback_result.get_future();
		settings.load(missing, [&callback_result](riwo::error_code error)
		{
			callback_result.set_value(error);
		});
		RIWO_TEST_CHECK_EQ(wait_for(callback_future), expected_error);

		if( sequence.bounded(3) == 0 )
			std::this_thread::yield();
	}

	RIWO_TEST_CHECK_EQ(loaded_count.load(), rounds * token_kinds);
	RIWO_TEST_CHECK_EQ(loaded_errors.load(), rounds * token_kinds);
	settings.loaded.disconnect();
}

void child_process_io()
{
	riwo::utils::process process;
#if defined(_WIN32)
	auto started = process.start("cmd.exe", "/C", "echo", "riwo-process-test");
#else
	auto started = process.start("/bin/echo", "riwo-process-test");
#endif
	RIWO_TEST_CHECK(started.has_value());
	RIWO_TEST_CHECK(process.joinable());
	RIWO_TEST_CHECK(process.pid() != 0);
	const auto output = process.read<std::string>();
	const auto exit_code = process.join();
	RIWO_TEST_CHECK_EQ(exit_code, 0);
	RIWO_TEST_CHECK(output.find("riwo-process-test") != std::string::npos);
	RIWO_TEST_CHECK_EQ(process.state(), riwo::utils::process_state::exited);
	RIWO_TEST_CHECK_EQ(process.exit_code(), 0);
	RIWO_TEST_CHECK(riwo::utils::process::self_pid().value_or(0) != 0);
}

void child_process_completed_single_byte_read()
{
#if defined(__unix__)
	riwo::utils::process process("/bin/sh", "-c", "printf x");
	RIWO_TEST_CHECK_EQ(process.run(), 0);

	std::array<char,8> output {};
	std::error_code error;
	const auto size = process.read(asio::buffer(output), error);

	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(size, 1);
	RIWO_TEST_CHECK_EQ(output[0], 'x');

	RIWO_TEST_CHECK_EQ(process.read(asio::buffer(output), error), 0);
	RIWO_TEST_CHECK_EQ(error,
		asio::error::make_error_code(asio::error::eof));
#endif
}

void child_process_environment_and_channels()
{
	riwo::test::temporary_directory directory;
	riwo::utils::process process;
	process.set_work_path(directory.path());
	process.setenv("RIWO_PROCESS_VALUE", "from-child");
#if defined(_WIN32)
	auto started = process.start("cmd.exe", "/C",
		"echo %RIWO_PROCESS_VALUE% & cd & echo stderr-value 1>&2");
#else
	auto started = process.start("/bin/sh", "-c",
		"printf '%s\\n' \"$RIWO_PROCESS_VALUE\"; pwd; printf 'stderr-value\\n' >&2");
#endif
	RIWO_TEST_CHECK(started);
	const auto output = process.read<std::string>();
	const auto errors = process.read_stderr<std::string>();
	RIWO_TEST_CHECK_EQ(process.join(), 0);
	RIWO_TEST_CHECK(output.find("from-child") != std::string::npos);
	RIWO_TEST_CHECK(output.find(directory.path().string()) != std::string::npos);
	RIWO_TEST_CHECK(errors.find("stderr-value") != std::string::npos);

	process.unsetenv("RIWO_PROCESS_VALUE");
#if defined(_WIN32)
	const riwo::utils::process::args_t args {"/C", "exit", "7"};
	RIWO_TEST_CHECK_EQ(riwo::utils::process::exec("cmd.exe", args).value_or(-1), 7);
	RIWO_TEST_CHECK_EQ(
		riwo::utils::process::exec("cmd.exe /C \"exit 6\"").value_or(-1), 6);
#else
	const riwo::utils::process::args_t args {"-c", "exit 7"};
	RIWO_TEST_CHECK_EQ(riwo::utils::process::exec("/bin/sh", args).value_or(-1), 7);
	RIWO_TEST_CHECK_EQ(
		riwo::utils::process::exec("/bin/sh -c 'exit 6'").value_or(-1), 6);
#endif
}

void child_process_state_errors()
{
	using namespace std::chrono_literals;
	riwo::utils::process process;
	RIWO_TEST_CHECK_EQ(process.state(), riwo::utils::process_state::idle);
	RIWO_TEST_CHECK(not process.joinable());

#if defined(_WIN32)
	RIWO_TEST_CHECK(process.start("cmd.exe", "/C", "ping -n 2 127.0.0.1 >nul"));
#else
	RIWO_TEST_CHECK(process.start("/bin/sh", "-c", "sleep 0.05"));
#endif
	auto duplicate = process.start();
	RIWO_TEST_CHECK(not duplicate);
	RIWO_TEST_CHECK(duplicate.error() == std::errc::device_or_resource_busy);
	RIWO_TEST_CHECK_THROWS(process.join(1ms), std::system_error);
	process.kill();
	std::error_code join_error;
	RIWO_TEST_CHECK_EQ(process.join(join_error), 0);
	RIWO_TEST_CHECK(join_error == std::errc::io_error);
	RIWO_TEST_CHECK_EQ(process.state(), riwo::utils::process_state::crashed);
	RIWO_TEST_CHECK(process.exit_code() != 0);
	RIWO_TEST_CHECK(not process.joinable());
}

void child_process_uses_immediate_executor_on_early_error()
{
	riwo::io_context_t process_context;
	riwo::io_context_t completion_context;
	riwo::utils::process process(process_context);

	bool completed = false;
	const std::string input = "not-running";
	process.write(asio::buffer(input),
		asio::bind_immediate_executor(completion_context.get_executor(),
		[&](riwo::error_code error, size_t transferred)
		{
			RIWO_TEST_CHECK(error);
			RIWO_TEST_CHECK_EQ(transferred, 0U);
			completed = true;
	}));

	RIWO_TEST_CHECK(not completed);
	RIWO_TEST_CHECK_EQ(process_context.poll(), 0U);
	RIWO_TEST_CHECK(not completed);
	completion_context.run();
	RIWO_TEST_CHECK(completed);
}

void child_process_cancel_options()
{
	using process_t = riwo::utils::process;
	using cancel_option = process_t::cancel_option;

	auto check = [](cancel_option option, bool remains_joinable)
	{
		riwo::io_context_t context;
	#if defined(_WIN32)
		process_t process(context, "cmd.exe", "/C",
			"ping -n 6 127.0.0.1 >nul");
	#else
		process_t process(context, "/bin/sh", "-c", "sleep 5");
	#endif
		RIWO_TEST_CHECK(process.start());
		const auto pid = process.pid();
		riwo::error_code run_error;
		auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			co_await process.join(asio::redirect_error (
				riwo::use_awaitable, run_error
			));
		}, asio::use_future);

		asio::steady_timer timer(context, 5ms);
		timer.async_wait([&](const std::error_code &error)
		{
			if( not error )
				process.cancel(option);
		});
		context.run();
		completed.get();

		RIWO_TEST_CHECK(run_error);
		RIWO_TEST_CHECK_EQ(process.joinable(), remains_joinable);
		if( remains_joinable )
		{
			RIWO_TEST_CHECK_EQ(process.state(), process_t::state_t::running);
			process.cancel(cancel_option::kill);
		}
		else if( option == cancel_option::detach )
		{
			// A detached child keeps running until explicitly stopped by PID.
			RIWO_TEST_CHECK(process_t::kill(pid).has_value());
		}

		// Let the background monitor reap the released child while its executor
		// is still alive.
		context.restart();
		asio::steady_timer cleanup_delay(context, 50ms);
		cleanup_delay.async_wait([](const std::error_code&) {});
		context.run();
	};

	check(cancel_option::none, true);
	check(cancel_option::terminate, false);
	check(cancel_option::kill, false);
	check(cancel_option::detach, false);
}

void local_message_bus()
{
	constexpr std::string_view topic = "riwo.test.counter";
	asio::thread_pool pool(1);
	std::atomic_bool received = false;
	std::atomic_int received_value = 0;
	riwo::utils::sbus::local_subscriber subscriber(pool);
	const auto sid = subscriber.subscribe(topic, [&](int value)
	{
		received_value = value;
		received = true;
	});
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(topic, 42);
	RIWO_TEST_CHECK(wait_for(received));
	RIWO_TEST_CHECK_EQ(received_value.load(), 42);

	std::atomic_bool changed = false;
	std::atomic_size_t current_size = 0;
	std::atomic_size_t previous_size = 0;
	std::atomic_int decoded_current = 0;
	std::atomic_bool decoded = false;
	riwo::utils::sbus::local_cache cache(pool);
	cache.changed(topic).connect([&](std::vector<std::byte> current,
		std::vector<std::byte> previous) -> riwo::awaitable<void>
	{
		current_size = current.size();
		previous_size = previous.size();
		changed = true;
		co_return;
	});
	cache.changed(topic).connect([&](int current, int) {
		decoded_current = current;
		decoded = true;
	});
	cache.set(topic, 7);
	RIWO_TEST_CHECK_EQ(cache.get<int>(topic).value_or(0), 7);
	RIWO_TEST_CHECK(wait_for(changed));
	RIWO_TEST_CHECK(wait_for(decoded));
	RIWO_TEST_CHECK_EQ(decoded_current.load(), 7);
	RIWO_TEST_CHECK_EQ(current_size.load(), sizeof(int));
	RIWO_TEST_CHECK(previous_size == 0 or previous_size == sizeof(int));

	received = false;
	subscriber.cancel_sid(sid);
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(topic, 99);
	std::this_thread::sleep_for(5ms);
	RIWO_TEST_CHECK(not received);
	cache.changed(topic).disconnect();
	pool.stop();
	pool.join();
}

void logger_configuration()
{
	using logger = riwo::utils::logger;
	auto &instance = logger::instance("riwo-test-runtime");
	logger::config_t config;
	config.level.console = logger::level_t::off;
	config.level.daily = logger::level_t::off;
	config.time_mode = logger::time_mode_t::utc;
	config.line_break = true;
	instance.set_config(config);

	const auto restored = instance.config();
	RIWO_TEST_CHECK_EQ(restored.level.console, logger::level_t::off);
	RIWO_TEST_CHECK_EQ(restored.level.daily, logger::level_t::off);
	RIWO_TEST_CHECK_EQ(restored.time_mode, logger::time_mode_t::utc);
	RIWO_TEST_CHECK(restored.line_break);
	RIWO_TEST_CHECK_EQ(instance.name(), "riwo-test-runtime");
	instance.info(logger::source_loc(__FILE__, __func__, __LINE__), "suppressed message");
	const auto names = logger::names();
	RIWO_TEST_CHECK(std::ranges::find(names, "riwo-test-runtime") != names.end());
}

void logger_file_flush()
{
	using logger = riwo::utils::logger;
	riwo::test::temporary_directory directory;
	auto &instance = logger::instance("riwo-test-runtime-file");
	logger::config_t config;
	config.path = directory.path();
	config.level.console = logger::level_t::off;
	config.level.daily = logger::level_t::info;
	instance.set_config(config);
	instance.info(
		logger::source_loc(__FILE__, __func__, __LINE__), "  queued message  "
	).flush();

	config.path.clear();
	instance.set_config(config);
	std::string output;
	for(const auto &entry : std::filesystem::recursive_directory_iterator(directory.path()))
	{
		if( not entry.is_regular_file() )
			continue;
		std::ifstream stream(entry.path());
		output.append(
			std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()
		);
	}
	RIWO_TEST_CHECK(output.find(": queued message") != std::string::npos);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"settings persistence and signals", settings_persistence_and_signals},
		{"settings IO tokens", settings_io_tokens},
		{"settings error-token lifetime stress", settings_error_token_lifetime_stress},
		{"child process IO", child_process_io},
		{"completed child single-byte read", child_process_completed_single_byte_read},
		{"child process environment and channels", child_process_environment_and_channels},
		{"child process state errors", child_process_state_errors},
		{"child process immediate executor on early error",
			child_process_uses_immediate_executor_on_early_error},
		{"child process cancel options", child_process_cancel_options},
		{"local message bus", local_message_bus},
		{"logger configuration", logger_configuration},
		{"logger file flush", logger_file_flush},
	});
}
