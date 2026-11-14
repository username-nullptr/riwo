// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/system/process.h>

#include <array>
#include <chrono>
#include <future>

namespace
{

using namespace std::chrono_literals;

static_assert(riwo::test::canonical_executor_type<riwo::system::process>);

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
	return riwo::test::run(argc, argv, {
		{"child process IO", child_process_io},
		{"completed child single-byte read", child_process_completed_single_byte_read},
		{"child process environment and channels", child_process_environment_and_channels},
		{"child process state errors", child_process_state_errors},
		{"child process immediate executor on early error",
			child_process_uses_immediate_executor_on_early_error},
		{"child process cancel options", child_process_cancel_options},
	});
}
