// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/executable.h>
#include <riwo/system/process.h>
#include <iostream>

int main(int argc, char *argv[])
try
{
	if( argc == 3 and std::string_view(argv[1]) == "--child" )
	{
		std::cout << "Received: " << argv[2] << '\n';
		std::cerr << "Message on stderr\n";
		return 0;
	}
	// Run this executable in child mode so the example has no external dependency.
	const auto executable = riwo::executable::path();
	riwo::sys_expected_loc_throw(executable, "Locate this executable");

	riwo::system::process_capture_options options;
	options.timeout = std::chrono::seconds(5);
	options.max_stdout_bytes = 1024;
	options.max_stderr_bytes = 1024;

	const auto result = riwo::system::process::capture (
		*executable, {"--child", "spaces & punctuation"}, options
	);
	riwo::sys_expected_loc_throw(result, "Capture child output");

	std::cout
		<< "Child stdout: " << result->stdout_data
		<< "Child stderr: " << result->stderr_data
		<< "Child exit code: " << result->exit_code << '\n';

	if (result->stdout_truncated or result->stderr_truncated)
		std::cout << "Captured output was truncated\n";

	return result->exit_code;
}
catch (const std::exception &exception)
{
	std::cerr << exception.what() << '\n';
	return 1;
}
