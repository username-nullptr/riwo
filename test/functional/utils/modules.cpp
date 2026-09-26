// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/modules.h>

int main()
{
	std::mutex mutex;
	std::vector<std::string> order;
	riwo::utils::modules::reg_init("database", [&]
	{
		std::scoped_lock lock(mutex);
		order.emplace_back("database");
		return true;
	});
	riwo::utils::modules::reg_init("service", {
		.parents = {"database"}
	}, [&](const riwo::string_vector &arguments)
	{
		RIWO_TEST_CHECK_EQ(arguments.size(), 2U);
		std::scoped_lock lock(mutex);
		order.emplace_back("service");
	});
	riwo::utils::modules::reg_init("failing", [] { return false; });
	riwo::utils::modules::reg_init("blocked", {
		.parents = {"failing"}
	}, [] {});
	riwo::utils::modules::reg_init("orphan", {
		.parents = {"missing-parent"}
	}, [] {});
	RIWO_TEST_CHECK_THROWS(
		riwo::utils::modules::reg_init("database", [] {}),
		riwo::runtime_error
	);
	RIWO_TEST_CHECK_THROWS(
		riwo::utils::modules::reg_init("", [] {}),
		riwo::runtime_error
	);

	const auto graph = riwo::utils::modules::sprint();
	RIWO_TEST_CHECK(graph.find("database") != std::string::npos);
	RIWO_TEST_CHECK(graph.find("service") != std::string::npos);
	auto unexpected = riwo::utils::modules::do_init(
		riwo::string_vector {"--test", "value"}
	);
	RIWO_TEST_CHECK_EQ(unexpected.failures, std::vector<std::string> {"failing"});
	RIWO_TEST_CHECK_EQ(unexpected.unregistered,
		std::vector<std::string> {"missing-parent"});
	std::ranges::sort(unexpected.children);
	RIWO_TEST_CHECK_EQ(unexpected.children,
		(std::vector<std::string> {"blocked", "orphan"}));
	RIWO_TEST_CHECK_EQ(order.size(), 2U);
	RIWO_TEST_CHECK_EQ(order[0], "database");
	RIWO_TEST_CHECK_EQ(order[1], "service");
	return 0;
}
