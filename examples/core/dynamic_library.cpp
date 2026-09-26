// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/system/app_utls.h>
#include <riwo/core/system/library.h>

#include <filesystem>
#include <iostream>

namespace
{
std::filesystem::path default_plugin_path()
{
	auto directory = riwo::app::dir_path();
	if(not directory)
		throw std::system_error(directory.error());

#if defined(_WIN32)
	constexpr auto file_name = "dynamic_library_plugin.dll";
#elif defined(__APPLE__)
	constexpr auto file_name = "dynamic_library_plugin.dylib";
#else
	constexpr auto file_name = "dynamic_library_plugin.so";
#endif
	return *directory / file_name;
}
}

int main(int argc, const char *argv[])
{
	try {
		const auto path = argc > 1 ? std::filesystem::path(argv[1])
			: default_plugin_path();

		riwo::library plugin(path);
		if(auto result = plugin.load(); not result)
			throw std::system_error(result.error());

		auto twice = plugin.interface<int(int)>("riwo_example_twice");
		if(not twice)
		{
			std::cerr << "Plugin symbol was not found\n";
			return 1;
		}
		std::cout << "Plugin path: " << plugin.file_name() << '\n';
		std::cout << "twice(21) = " << (*twice)(21) << '\n';
	}
	catch(const std::exception &exception)
	{
		std::cerr << "Dynamic-library example failed: " << exception.what() << '\n';
		return 1;
	}
	return 0;
}
