// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/process.h>
#include <riwo/system/temporary.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <string_view>

int main()
try
{
	using namespace riwo::system;

	const auto output = temp_file::create();
	riwo::sys_expected_loc_throw(output, "Create temporary output file");

	process_stdio streams;
	streams.output = {
		.mode = process_stream_mode::file,
		.path = output->path()
	};
	streams.error.mode = process_stream_mode::inherit;

	process child;
	child.set_stdio(streams);

#if defined(_WIN32)
	const auto started = child.start("sort.exe");
#else
	const auto started = child.start("sort");
#endif
	riwo::sys_expected_loc_throw(started, "Start sort");

	if( constexpr std::string_view lines = "banana\napple\n";
		child.write(asio::buffer(lines)) != lines.size() )
	{
		std::cerr << "Incomplete write to child stdin\n";
		return 1;
	}
	// The child reads until EOF before it finishes sorting.
	riwo::sys_expected_loc_throw(child.close_stdin(), "Close child stdin");

	if( const auto exit_code = child.join(); exit_code != 0 )
	{
		std::cerr << "sort exited with code " << exit_code << '\n';
		return exit_code;
	}
	std::ifstream sorted(output->path());
	if( not sorted )
	{
		std::cerr << "Could not read sorted output\n";
		return 1;
	}
	std::cout << "Sorted output:\n" << sorted.rdbuf();
	return 0;
}
catch (const std::exception &exception)
{
	std::cerr << exception.what() << '\n';
	return 1;
}
