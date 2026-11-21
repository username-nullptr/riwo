// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/directory_watcher.h>
#include <riwo/system/temporary.h>
#include <iostream>

int main()
try
{
	using namespace std::chrono_literals;
	using namespace riwo::system;

	const auto scratch = temp_directory::create();
	riwo::sys_expected_loc_throw(scratch, "Create watched directory");

	asio::io_context context;
	auto watcher = directory_watcher<>::open(context, scratch->path());
	riwo::sys_expected_loc_throw(watcher, "Open directory watcher");

	auto file = temp_file::create(scratch->path());
	riwo::sys_expected_loc_throw(file, "Create watched file");

	// Synchronous waits scan without running the I/O context.
	for(const auto &[kind, path] : watcher->wait(2s))
	{
		if (kind == directory_change_kind::created)
			std::cout << "Created: " << path << '\n';
	}
	// The asynchronous wait completes on the watcher's context.
	auto pending = watcher->wait(riwo::redirect_time(riwo::use_future, 2s));
	riwo::sys_expected_loc_throw(file->remove(), "Remove watched file");

	context.run();
	for(const auto &[kind, path] : pending.get())
	{
		if( kind == directory_change_kind::removed )
			std::cout << "Removed: " << path << '\n';
	}
	return 0;
}
catch (const std::exception &exception)
{
	std::cerr << exception.what() << '\n';
	return 1;
}
