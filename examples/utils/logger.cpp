// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/logger.h>
#include <filesystem>
#include <iostream>

int main(int argc, const char *argv[])
{
	const std::filesystem::path log_directory = argc > 1 ?
		argv[1] : "./logs";

	riwo::utils::logger::config_t config {
		.path = log_directory
	};
	riwo::utils::logger::instance().set_config(config);
	riwo::utils::logger::instance("network").set_config(config);

	riwo_utils_log_info("Application logger: {}", "ready");
	riwo_utils_clog_info("network", "Named logger: request {}", 42);

	std::cout << "Logs are written below " << log_directory << '\n';
	return 0;
}
