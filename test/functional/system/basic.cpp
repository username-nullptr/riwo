// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include "scoped_environment.h"

#include <riwo/system.h>

#include <fstream>
#include <future>
#include <thread>

namespace
{

using namespace std::chrono_literals;
namespace fs = std::filesystem;

void environment()
{
	constexpr std::string_view key = "RIWO_TEST_SYSTEM_ENVIRONMENT";
	constexpr std::string_view first = "available/environment-value";

	RIWO_TEST_CHECK(riwo::system::environment::set(key, first));
	RIWO_TEST_CHECK(riwo::system::environment::set(key, "second", false));

	const auto value = riwo::system::environment::get(key);
	RIWO_TEST_CHECK(value and value->has_value());
	if( value and value->has_value() )
		RIWO_TEST_CHECK_EQ(**value, first);

	const auto values = riwo::system::environment::all();
	RIWO_TEST_CHECK(values and values->contains(std::string(key)));

	RIWO_TEST_CHECK(riwo::system::environment::unset(key));
	const auto removed = riwo::system::environment::get(key);
	RIWO_TEST_CHECK(removed and not removed->has_value());
}

void user()
{
	RIWO_TEST_CHECK(riwo::system::current_user_name());
	const auto home = riwo::system::home_directory();
	RIWO_TEST_CHECK(home and not home->empty());
	const auto expanded = riwo::system::expand_user_path("~/riwo");
	RIWO_TEST_CHECK(expanded and home and
		*expanded == (*home / "riwo").lexically_normal());
}

void application_directories()
{
	using riwo::system::app_paths;
	RIWO_TEST_CHECK(not app_paths(""));
	RIWO_TEST_CHECK(not app_paths("../riwo"));
	RIWO_TEST_CHECK(not app_paths("foo\\bar"));
	RIWO_TEST_CHECK(not app_paths("NUL.txt"));

#ifndef _WIN32
	riwo::test::scoped_environment saved {
		"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"
	};
	const auto root = riwo::system::temp_directory::create().value();
	RIWO_TEST_CHECK(riwo::system::environment::set("HOME", root.path().string()));
	RIWO_TEST_CHECK(riwo::system::environment::set("XDG_CONFIG_HOME", (root.path() / "config").string()));
	RIWO_TEST_CHECK(riwo::system::environment::set("XDG_CACHE_HOME", "ignored-relative"));
	RIWO_TEST_CHECK(riwo::system::environment::set("XDG_STATE_HOME", (root.path() / "state").string()));
	RIWO_TEST_CHECK(riwo::system::environment::unset("XDG_DATA_HOME"));
#endif
	const auto paths = app_paths("org.riwo-test");
	RIWO_TEST_CHECK(paths);
	RIWO_TEST_CHECK(paths->config.is_absolute() and paths->data.is_absolute());
	RIWO_TEST_CHECK(paths->state.is_absolute() and paths->cache.is_absolute());
#ifdef _WIN32
	RIWO_TEST_CHECK_EQ(paths->config.filename(), fs::path("config"));
	RIWO_TEST_CHECK_EQ(paths->data.filename(), fs::path("data"));
	RIWO_TEST_CHECK_EQ(paths->cache.filename(), fs::path("cache"));
	RIWO_TEST_CHECK_EQ(paths->state.filename(), fs::path("state"));
#else
	RIWO_TEST_CHECK_EQ(paths->config, root.path() / "config/org.riwo-test");
	RIWO_TEST_CHECK_EQ(paths->data, root.path() / ".local/share/org.riwo-test");
	RIWO_TEST_CHECK_EQ(paths->state, root.path() / "state/org.riwo-test");
	RIWO_TEST_CHECK_EQ(paths->cache, root.path() / ".cache/org.riwo-test");
	RIWO_TEST_CHECK(fs::is_empty(root.path()));
#endif
}

void temporary_resources()
{
	using riwo::system::temp_directory;
	using riwo::system::temp_file;
	auto root = temp_directory::create().value();
	const auto root_path = root.path();
	fs::path kept;
	fs::path cleaned;
	{
		auto first = temp_file::create(root_path).value();
		auto second = temp_file::create(root_path).value();
		const auto first_path = first.path();
		RIWO_TEST_CHECK(first_path.is_absolute() and fs::is_regular_file(first_path));
		RIWO_TEST_CHECK(first_path != second.path());
		cleaned = second.path();
		first = std::move(second);
		RIWO_TEST_CHECK(second.path().empty());
		RIWO_TEST_CHECK(not fs::exists(first_path) and fs::exists(cleaned));

		auto directory = temp_directory::create(root_path).value();
		std::ofstream(directory.path() / "nested") << "content";
		kept = directory.release();
	}
	RIWO_TEST_CHECK(not fs::exists(cleaned));
	RIWO_TEST_CHECK(fs::exists(kept / "nested"));
	RIWO_TEST_CHECK(not temp_file::create(root_path / "missing"));
	RIWO_TEST_CHECK(root.remove());
	RIWO_TEST_CHECK(not fs::exists(root_path));
}

void watch_directory_changes()
{
	using riwo::system::directory_change_kind;
	using directory_watcher = riwo::system::directory_watcher<riwo::io_executor_t>;
	auto root = riwo::system::temp_directory::create().value();
	const auto file = root.path() / fs::path(u8"配置.ini");
	riwo::io_context_t context;
	const auto executor = context.get_executor();
	RIWO_TEST_CHECK(not directory_watcher::open(executor, root.path(), 0ms));
	auto watcher = directory_watcher::open(context, root.path(), 2ms).value();
	static_assert(std::same_as<decltype(watcher.get_executor()),riwo::io_executor_t>);
	RIWO_TEST_CHECK(watcher.get_executor() == context.get_executor());

	auto check_change = [&](directory_change_kind kind, const auto &change) {
		change();
		context.restart();
		directory_watcher::changes_t changes;
		riwo::error_code error;
		bool completed = false;
		watcher.wait([&](riwo::error_code result, directory_watcher::changes_t value) {
			error = result;
			changes = std::move(value);
			completed = true;
		});
		context.run_for(1s);
		if( not completed )
		{
			watcher.cancel();
			context.run();
		}
		RIWO_TEST_CHECK(completed and not error);
		RIWO_TEST_CHECK_EQ(changes.size(), 1U);
		RIWO_TEST_CHECK_EQ(changes.front().kind, kind);
		RIWO_TEST_CHECK_EQ(changes.front().path, file);
	};

	check_change(directory_change_kind::created, [&] {
		std::ofstream(file) << "version 1";
	});
	check_change(directory_change_kind::modified, [&] {
		const auto modified = fs::last_write_time(file);
		auto staging = riwo::system::temp_file::create(root.path()).value();
		std::ofstream(staging.path()) << "version 2";
		fs::last_write_time(staging.path(), modified);
		fs::rename(staging.path(), file);
	});
	check_change(directory_change_kind::removed, [&] {
		fs::remove(file);
	});

	context.restart();
	riwo::error_code wait_error;
	riwo::error_code duplicate_error;
	watcher.wait([&](riwo::error_code error, directory_watcher::changes_t) {
		wait_error = error;
	});
	watcher.wait([&](riwo::error_code error, directory_watcher::changes_t) {
		duplicate_error = error;
	});
	context.poll();
	std::error_code sync_error;
	const auto busy = watcher.wait(sync_error);
	RIWO_TEST_CHECK(busy.empty() and sync_error == std::errc::device_or_resource_busy);
	watcher.cancel();
	context.run();
	RIWO_TEST_CHECK_EQ(wait_error, asio::error::make_error_code(asio::error::operation_aborted));
	RIWO_TEST_CHECK(duplicate_error == std::errc::device_or_resource_busy);

	check_change(directory_change_kind::created, [&] {
		std::ofstream(file) << "version 3";
	});
}

void watch_directory_sync()
{
	using riwo::system::directory_change_kind;
	using directory_watcher = riwo::system::directory_watcher<>;
	auto root = riwo::system::temp_directory::create().value();
	const auto file = root.path() / "config.ini";
	riwo::io_context_t context;
	auto watcher = directory_watcher::open(root.path(), 2ms).value();

	std::ofstream(file) << "initial";
	RIWO_TEST_CHECK_EQ(watcher.wait().at(0).kind, directory_change_kind::created);
	std::ofstream(file) << "modified";
	RIWO_TEST_CHECK_EQ(watcher.wait(riwo::use_sync).at(0).kind, directory_change_kind::modified);
	fs::remove(file);
	std::error_code error = std::make_error_code(std::errc::io_error);
	const auto removed = watcher.wait(error);
	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(removed.at(0).kind, directory_change_kind::removed);

	std::jthread writer([&] {
		std::this_thread::sleep_for(10ms);
		std::ofstream(file) << "recreated";
	});
	RIWO_TEST_CHECK_EQ(watcher.wait(1s).at(0).path, file);
	writer.join();

	std::ofstream(file) << "longer configuration";
	RIWO_TEST_CHECK_EQ(watcher.wait(std::chrono::steady_clock::now() + 1s).at(0).kind,
		directory_change_kind::modified);
	const auto expect_timeout = [&](const auto &timeout) {
		try {
			RIWO_UNUSED(watcher.wait(timeout));
			RIWO_TEST_CHECK(false);
		}
		catch(const std::system_error &exception) {
			RIWO_TEST_CHECK(exception.code() == std::errc::timed_out);
		}
	};
	expect_timeout(8ms);
	expect_timeout(std::chrono::system_clock::now() - 1s);

	fs::remove(file);
	const auto [tuple_error, tuple_changes] = watcher.wait(riwo::as_tuple(riwo::use_sync));
	RIWO_TEST_CHECK(not tuple_error);
	RIWO_TEST_CHECK_EQ(tuple_changes.at(0).kind, directory_change_kind::removed);

	std::ofstream(file) << "expected";
	const auto expected = watcher.wait(riwo::as_expected(riwo::use_sync));
	RIWO_TEST_CHECK(expected);
	RIWO_TEST_CHECK_EQ(expected->at(0).kind, directory_change_kind::created);

	auto cancel_target = directory_watcher::open(context.get_executor(), root.path(), 10s).value();
	auto pending = std::async(std::launch::async, [&] {
		std::error_code wait_error;
		return cancel_target.wait(wait_error).empty() ? wait_error : std::error_code {};
	});
	while( pending.wait_for(5ms) != std::future_status::ready )
		cancel_target.cancel();
	RIWO_TEST_CHECK_EQ(pending.get(), asio::error::make_error_code(asio::error::operation_aborted));
}

void watch_directory_completion_tokens()
{
	using riwo::system::directory_change_kind;
	using directory_watcher = riwo::system::directory_watcher<>;
	auto root = riwo::system::temp_directory::create().value();
	const auto file = root.path() / "config.ini";
	riwo::io_context_t context;
	auto watcher = directory_watcher::open(context.get_executor(), root.path(), 2ms).value();

	const auto complete = [&](auto future) {
		context.restart();
		context.run_for(1s);
		RIWO_TEST_CHECK(future.wait_for(0ms) == std::future_status::ready);
		return future.get();
	};
	std::ofstream(file) << "created";
	RIWO_TEST_CHECK_EQ(complete(watcher.wait(riwo::use_future)).at(0).kind, directory_change_kind::created);

	std::ofstream(file) << "new contents";
	auto coroutine = asio::co_spawn(context,
		[&]() -> riwo::awaitable<directory_watcher::changes_t> {
			co_return co_await watcher.wait(riwo::use_awaitable);
		}, riwo::use_future);
	RIWO_TEST_CHECK_EQ(complete(std::move(coroutine)).at(0).kind, directory_change_kind::modified);

	auto deferred = watcher.wait(riwo::deferred);
	watcher.cancel();
	fs::remove(file);
	RIWO_TEST_CHECK_EQ(complete(std::move(deferred)(riwo::use_future)).at(0).kind,
		directory_change_kind::removed);

	std::ofstream(file) << "detached";
	watcher.wait(riwo::detached);
	context.restart();
	context.run();
	RIWO_TEST_CHECK_THROWS(watcher.wait(-1ms), std::system_error);

	riwo::error_code timed_error;
	auto timed = watcher.wait(riwo::redirect_time(asio::redirect_error(riwo::use_future, timed_error), 10ms));
	RIWO_TEST_CHECK(complete(std::move(timed)).empty() and timed_error == std::errc::timed_out);

	auto timed_deferred = watcher.wait(riwo::redirect_time(riwo::deferred, 10ms));
	const auto deferred_result = complete(std::move(timed_deferred)(riwo::as_expected(riwo::use_future)));
	RIWO_TEST_CHECK(not deferred_result and deferred_result.error() == std::errc::timed_out);

	asio::cancellation_signal signal;
	context.restart();
	auto cancelled = watcher.wait(asio::bind_cancellation_slot(
		signal.slot(), riwo::as_tuple(riwo::use_future)));
	context.poll();
	signal.emit(asio::cancellation_type::terminal);
	const auto [cancel_error, changes] = complete(std::move(cancelled));
	RIWO_TEST_CHECK_EQ(cancel_error, asio::error::make_error_code(asio::error::operation_aborted));
	RIWO_TEST_CHECK(changes.empty());

	fs::remove(file);
	RIWO_TEST_CHECK_EQ(watcher.wait().at(0).path, file);

	auto replaced = directory_watcher::open(context, root.path(), 10s).value();
	context.restart();
	auto replaced_wait = replaced.wait(riwo::as_expected(riwo::use_future));
	auto transferred_wait = watcher.wait(riwo::use_future);
	context.poll();
	replaced = std::move(watcher);

	watcher.cancel();
	std::error_code moved_error;
	RIWO_TEST_CHECK(watcher.wait(moved_error).empty() and moved_error == std::errc::invalid_argument);
	auto moved_wait = watcher.wait(riwo::as_expected(riwo::use_future));

	std::ofstream(file) << "transferred";
	RIWO_TEST_CHECK_EQ(complete(std::move(transferred_wait)).at(0).path, file);
	const auto replaced_result = complete(std::move(replaced_wait));
	RIWO_TEST_CHECK(not replaced_result and replaced_result.error() == asio::error::operation_aborted);
	const auto moved_result = complete(std::move(moved_wait));
	RIWO_TEST_CHECK(not moved_result and moved_result.error() == std::errc::invalid_argument);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"environment", environment},
		{"user", user},
		{"application directories", application_directories},
		{"temporary resources", temporary_resources},
		{"watch directory changes", watch_directory_changes},
		{"watch directory synchronously", watch_directory_sync},
		{"watch directory completion tokens", watch_directory_completion_tokens},
	});
}
