// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/args_parser.h>
#include <iostream>

int main(int argc, const char *argv[])
{
	riwo::string_vector positional;
	auto options = riwo::cmdline::args_parser("Riwo command-line example")
		.add_group("-o,--output", "Output file", "path")
		.add_flag("-v,--verbose", "Enable verbose output", "verbose")
		.set_version("Riwo example 1.0")
		.enable_h()
		.parsing(argc, argv, positional);

	for(const auto &[name, value] : options)
		std::cout << name << " = " << value.to_string() << '\n';

	for(const auto &argument : positional)
		std::cout << "positional = " << argument << '\n';
	return 0;
}
