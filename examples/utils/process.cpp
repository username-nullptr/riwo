// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/process.h>
#include <iostream>
#include <string>

int main()
{
	riwo::utils::process child;
#if defined(_WIN32)
	auto started = child.start("cmd.exe", "/C", "echo", "Hello from Riwo");
#else
	auto started = child.start("/bin/echo", "Hello from Riwo");
#endif
	if(not started)
	{
		std::cerr << "Failed to start child: "
			<< started.error().message() << '\n';
		return 1;
	}
	try {
		auto output = child.read<std::string>();
		auto exit_code = child.join();

		std::cout << "Child output: " << output;
		std::cout << "Child exit code: " << exit_code << '\n';
		return exit_code;
	}
	catch(const std::exception &exception) {
		std::cerr << "Child-process I/O failed: " << exception.what() << '\n';
	}
	return 1;
}
