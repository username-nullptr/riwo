// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/ini.h>
#include <filesystem>
#include <iostream>

int main(int argc, const char *argv[])
{
	const std::filesystem::path path = argc > 1 ?
		argv[1] : "riwo-example.ini";

	riwo::ini config(path);
	std::error_code error;

	config.load_or(error);
	if(error)
	{
		std::cerr << "Load failed: " << error.message() << '\n';
		return 1;
	}
	config.write("server/host", "127.0.0.1");
	config.write("server/port", 8080);
	config.write("features/logging", true);

	config.sync(error);
	if(error)
	{
		std::cerr << "Save failed: " << error.message() << '\n';
		return 1;
	}
	auto host = config.read("server/host");
	auto port = config.read("server/port");

	std::cout << "Saved " << path << '\n';
	std::cout << "Server: " << (host ? host->to_string() : "missing") << ':'
			<< (port ? port->to_int().value_or(0) : 0) << '\n';
	return 0;
}
