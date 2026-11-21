// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/file_lock.h>
#include <riwo/system/temporary.h>
#include <riwo/system/paths.h>
#include <iostream>

int main()
try
{
	using namespace riwo::system;

	const auto paths = app_paths("org.riwo.example");
	riwo::sys_expected_loc_throw(paths, "Resolve application paths");

	std::cout << "Config: " << paths->config << '\n'
		<< "Data: " << paths->data << '\n'
		<< "Cache: " << paths->cache << '\n'
		<< "State: " << paths->state << '\n';

	// Keep the demo's files in a temporary directory owned by this scope.
	const auto scratch = temp_directory::create();
	riwo::sys_expected_loc_throw(scratch, "Create temporary directory");

	const auto file = temp_file::create(scratch->path());
	riwo::sys_expected_loc_throw(file, "Create temporary file");

	// Cooperating writers use the same lock path for the shared resource.
	const auto lock_path = scratch->path() / "write.lock";
	auto lock = file_lock::try_lock(lock_path);

	riwo::sys_expected_loc_throw(lock, "Open file lock");
	if( not *lock )
	{
		std::cout << "Lock is busy: " << lock_path << '\n';
		return 0;
	}
	std::ofstream output(file->path());
	output.exceptions(std::ios::failbit | std::ios::badbit);

	output << "Hello from Riwo\n";
	output.close();

	std::cout << "Wrote " << file->path() << " while holding " << lock_path << '\n';
	return 0;
}
catch (const std::exception &exception)
{
	std::cerr << exception.what() << '\n';
	return 1;
}
