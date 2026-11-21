// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/executable.h>
#include <riwo/system/file_lock.h>
#include <riwo/system/process.h>
#include <riwo/system/temporary.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iterator>
#include <thread>

namespace
{

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using riwo::system::process;
using riwo::system::process_stream_mode;
using riwo::system::process_stdio;

static_assert(riwo::test::canonical_executor_type<riwo::system::process>);

fs::path fixture_command()
{
	auto filename = riwo::executable::path().value();
	fs::path command("\"");
	command += filename;
	command += "\"";
	return command;
}

int child_fixture(int argc, const char *const argv[])
{
	const std::string_view mode = argv[1];
	if( mode == "--capture" )
	{
		if( argc != 4 or std::string_view(argv[2]) != "" or std::string_view(argv[3]) != "  literal & arg  " )
			return 99;
		const std::string output(8192, 'o');
		const std::string errors(8192, 'e');
		for(int index = 0; index < 40; ++index)
		{
			std::cout << output << std::flush;
			std::cerr << errors;
		}
		return 7;
	}
	if( mode == "--sleep" or mode == "--sleep-after-eof" )
	{
		if( mode == "--sleep-after-eof" )
		{
			std::fclose(stdout);
			std::fclose(stderr);
		}
		std::this_thread::sleep_for(10s);
		return 0;
	}
	if( mode == "--working-directory" )
	{
		std::cout << fs::current_path().generic_string();
		return 0;
	}
	if( mode == "--copy-stdin" )
	{
		const std::string input(std::istreambuf_iterator<char>(std::cin), {});
		std::cout << input;
		return 0;
	}
	if( mode == "--print-channels" )
	{
		std::cout << "stdout" << std::flush;
		std::cerr << "stderr";
		return 0;
	}
	if( mode == "--inherit-channels" )
	{
		process child;
		process_stdio stdio;
		stdio.input.mode = process_stream_mode::inherit;
		stdio.output.mode = process_stream_mode::inherit;
		stdio.error.mode = process_stream_mode::inherit;
		child.set_stdio(std::move(stdio));
		return child.start(fixture_command(), "--print-channels") ? child.join() : 99;
	}
	if( mode == "--try-file-lock" and argc == 3 )
	{
		const auto attempt = riwo::system::file_lock::try_lock(argv[2]);
		return not attempt ? 99 : (*attempt ? 0 : 3);
	}
	return -1;
}

void child_process_capture()
{
	const auto fixture = riwo::executable::path().value();
	riwo::system::process_capture_options options;
	options.max_stdout_bytes = 12345;
	options.max_stderr_bytes = 0;
	const auto captured = process::capture(fixture, {"--capture", "", "  literal & arg  "}, options);
	RIWO_TEST_CHECK(captured);
	RIWO_TEST_CHECK_EQ(captured->exit_code, 7);
	RIWO_TEST_CHECK_EQ(captured->stdout_data, std::string(12345, 'o'));
	RIWO_TEST_CHECK(captured->stderr_data.empty());
	RIWO_TEST_CHECK(captured->stdout_truncated and captured->stderr_truncated);

	const auto inherited = process::capture(fixture, {"--inherit-channels"});
	RIWO_TEST_CHECK(inherited);
	RIWO_TEST_CHECK_EQ(inherited->exit_code, 0);
	RIWO_TEST_CHECK_EQ(inherited->stdout_data, "stdout");
	RIWO_TEST_CHECK_EQ(inherited->stderr_data, "stderr");
	RIWO_TEST_CHECK(not inherited->stdout_truncated and not inherited->stderr_truncated);

	const auto eof = process::capture(fixture, {"--copy-stdin"});
	RIWO_TEST_CHECK(eof and eof->exit_code == 0 and eof->stdout_data.empty());

	auto directory = riwo::system::temp_directory::create().value();
	options.work_path = directory.path();
	const auto work_directory = process::capture(fixture, {"--working-directory"}, options);
	RIWO_TEST_CHECK(work_directory and fs::path(work_directory->stdout_data) == directory.path());
	const auto missing = process::capture(directory.path() / "missing-program");
	RIWO_TEST_CHECK(not missing and missing.error() == std::errc::no_such_file_or_directory);

	options.timeout = 100ms;
	for(const auto mode : {"--sleep", "--sleep-after-eof"})
	{
		const auto timeout = process::capture(fixture, {mode}, options);
		RIWO_TEST_CHECK(not timeout and timeout.error() == std::errc::timed_out);
	}
}

void child_process_redirected_stdio()
{
	auto directory = riwo::system::temp_directory::create().value();
	const auto file = directory.path() / "stdout.txt";
	const auto input = directory.path() / "stdin.txt";
	std::ofstream(input, std::ios::binary) << "redirected input";
	std::ofstream(file, std::ios::binary) << "stale content to be truncated";

	auto read_file = [](const fs::path &path) {
		std::ifstream content(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(content), {});
	};
	process child;
	process_stdio stdio;
	stdio.input = {process_stream_mode::file, input};
	stdio.output = {process_stream_mode::file, file};
	stdio.error.mode = process_stream_mode::discard;
	child.set_stdio(stdio);
	RIWO_TEST_CHECK(child.start(fixture_command(), "--copy-stdin"));
	RIWO_TEST_CHECK_EQ(child.join(), 0);
	RIWO_TEST_CHECK_EQ(read_file(file), "redirected input");

	stdio.input.mode = process_stream_mode::discard;
	stdio.error = stdio.output;
	child.set_stdio(stdio);
	RIWO_TEST_CHECK(child.start(fixture_command(), "--print-channels"));
	RIWO_TEST_CHECK_EQ(child.join(), 0);
	RIWO_TEST_CHECK_EQ(read_file(file), "stdoutstderr");

	child.set_stdio({});
	RIWO_TEST_CHECK(child.start(fixture_command(), "--copy-stdin"));
	const std::string message = "close sends EOF";
	std::error_code error;
	const auto written = child.write(asio::buffer(message), error);
	const auto closed = child.close_stdin();
	const auto closed_again = child.close_stdin();
	const auto output = child.read<std::string>();
	RIWO_TEST_CHECK_EQ(child.join(), 0);
	RIWO_TEST_CHECK(not error and written == message.size());
	RIWO_TEST_CHECK(closed and closed_again);
	RIWO_TEST_CHECK_EQ(output, message);
}

void file_lock_across_processes()
{
	auto directory = riwo::system::temp_directory::create().value();
	const auto path = directory.path() / "shared.lock";
	const auto fixture = riwo::executable::path().value();
	auto held = riwo::system::file_lock::try_lock(path);
	RIWO_TEST_CHECK(held and *held and (**held).owns_lock());
	const auto busy = process::capture(fixture, {"--try-file-lock", path});
	RIWO_TEST_CHECK(busy and busy->exit_code == 3);
	held->reset();
	const auto acquired = process::capture(fixture, {"--try-file-lock", path});
	RIWO_TEST_CHECK(acquired and acquired->exit_code == 0);
	RIWO_TEST_CHECK(fs::exists(path));
}

void child_process_io()
{
	riwo::system::process process;
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
	RIWO_TEST_CHECK_EQ(process.state(), riwo::system::process_state::exited);
	RIWO_TEST_CHECK_EQ(process.exit_code(), 0);
	RIWO_TEST_CHECK(riwo::system::process::self_pid().value_or(0) != 0);
}

void child_process_completed_single_byte_read()
{
#if defined(__unix__)
	riwo::system::process process("/bin/sh", "-c", "printf x");
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
	riwo::system::process process;
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
	const riwo::system::process::args_t args {"/C", "exit", "7"};
	RIWO_TEST_CHECK_EQ(riwo::system::process::exec("cmd.exe", args).value_or(-1), 7);
	RIWO_TEST_CHECK_EQ(
		riwo::system::process::exec("cmd.exe /C \"exit 6\"").value_or(-1), 6);
#else
	const riwo::system::process::args_t args {"-c", "exit 7"};
	RIWO_TEST_CHECK_EQ(riwo::system::process::exec("/bin/sh", args).value_or(-1), 7);
	RIWO_TEST_CHECK_EQ(
		riwo::system::process::exec("/bin/sh -c 'exit 6'").value_or(-1), 6);
#endif
}

void child_process_state_errors()
{
	riwo::system::process process;
	RIWO_TEST_CHECK_EQ(process.state(), riwo::system::process_state::idle);
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
	RIWO_TEST_CHECK_EQ(process.state(), riwo::system::process_state::crashed);
	RIWO_TEST_CHECK(process.exit_code() != 0);
	RIWO_TEST_CHECK(not process.joinable());
}

void child_process_uses_immediate_executor_on_early_error()
{
	riwo::io_context_t process_context;
	riwo::io_context_t completion_context;
	riwo::system::process process(process_context);

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
	using process_t = riwo::system::process;
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

} //namespace

int main(int argc, const char *const argv[])
{
	if( argc >= 2 )
	{
		if( const auto result = child_fixture(argc, argv); result >= 0 )
			return result;
	}
	return riwo::test::run(argc, argv, {
		{"child process capture", child_process_capture},
		{"child process redirected stdio", child_process_redirected_stdio},
		{"file lock across processes", file_lock_across_processes},
		{"child process IO", child_process_io},
		{"completed child single-byte read", child_process_completed_single_byte_read},
		{"child process environment and channels", child_process_environment_and_channels},
		{"child process state errors", child_process_state_errors},
		{"child process immediate executor on early error",
			child_process_uses_immediate_executor_on_early_error},
		{"child process cancel options", child_process_cancel_options},
	});
}
