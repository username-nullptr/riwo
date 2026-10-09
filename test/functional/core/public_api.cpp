// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/async_expected.h>
#include <riwo/core/algorithm/math.h>
#include <riwo/core/container.h>
#include <riwo/core/string_deque.h>
#include <riwo/core/string_list.h>
#include <riwo/core/string_set.h>
#include <riwo/core/system.h>
#include <riwo/core/utils/formatter.h>
#include <riwo/core/utils/utils.h>

namespace
{

enum class feature : uint8_t {
	none = 0, read = 1, write = 2, execute = 4
};
RIWO_DECLARE_FLAGS(features, feature);
RIWO_DECLARE_OPERATORS_FOR_FLAGS(features);

class parameter_owner final : public riwo::mutable_parameters<parameter_owner>
{
public:
	parameter_owner() : mutable_parameters(&storage) {}
	riwo::parameter_map storage;
};

void flags_and_parameters()
{
	features selected {feature::read, feature::write};
	RIWO_TEST_CHECK(selected.test_flag(feature::read));
	RIWO_TEST_CHECK_EQ(selected.value<uint8_t>(), uint8_t {3});
	selected.set_flag(feature::write, false).set_flag(feature::execute);
	RIWO_TEST_CHECK(not selected.test_flag(feature::write));
	RIWO_TEST_CHECK(selected.test_flag(feature::execute));
	RIWO_TEST_CHECK_EQ(*(feature::read | feature::write), uint32_t {3});
	RIWO_TEST_CHECK_EQ(*(feature::read | features {feature::write}), uint32_t {3});
	RIWO_TEST_CHECK(!(selected & uint8_t {2}));

	parameter_owner owner;
	owner.set_parameter("first", 1).set_parameter(std::string("second"), "two");
	RIWO_TEST_CHECK_EQ(owner.parameter(0)->to_int().value_or(0), 1);
	RIWO_TEST_CHECK_EQ(owner.parameter("second")->to_string(), "two");
	RIWO_TEST_CHECK(owner.contains_parameter("first", 1));
	RIWO_TEST_CHECK(not owner.contains_parameter(2));
	RIWO_TEST_CHECK_THROWS(owner.parameter(2), riwo::runtime_error);

	const auto &const_map = owner.storage;
	RIWO_TEST_CHECK_EQ(const_map["first"].to_int().value_or(0), 1);
	RIWO_TEST_CHECK_THROWS(const_map["missing"], riwo::out_of_range);
	owner.unset_parameter("first").unset_parameter("missing");
	RIWO_TEST_CHECK(not owner.contains_parameter("first"));
}

void value_and_string_algorithms()
{
	riwo::value number("7f");
	RIWO_TEST_CHECK_EQ(number.to_uint(16).value_or(0), 127U);
	RIWO_TEST_CHECK_EQ(number.get<int>(size_t {16}).value_or(0), 127);
	RIWO_TEST_CHECK(number.is_alnum());
	RIWO_TEST_CHECK(not number.is_digit());
	RIWO_TEST_CHECK(riwo::value("-1.25").is_rlnum());
	RIWO_TEST_CHECK(riwo::value("ASCII").is_ascii());
	RIWO_TEST_CHECK(not riwo::strtls::is_rlnum("-"));
	RIWO_TEST_CHECK(not riwo::strtls::to_int8("128"));
	RIWO_TEST_CHECK(not riwo::strtls::to_arith<int8_t>("128", 10));
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_arith<int8_t>("127", 10).value_or(0), 127);
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_string(0), "0");
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_string(
		std::numeric_limits<int>::min()), std::to_string(std::numeric_limits<int>::min()));
	int unchanged = -42;
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_string(unchanged), "-42");
	RIWO_TEST_CHECK_EQ(unchanged, -42);
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_string(255, 16, true), "FF");

	size_t replacements = 0;
	RIWO_TEST_CHECK_EQ(
		riwo::strtls::replace(replacements, std::string("aaaa"), "aa", "b"),
		"bb"
	);
	RIWO_TEST_CHECK_EQ(replacements, 2U);
	RIWO_TEST_CHECK_EQ(riwo::strtls::remove(std::string("a-b-c"), '-'), "abc");

	const std::array projected {1, 2, 3};
	RIWO_TEST_CHECK_EQ(riwo::mean(projected.begin(), projected.end(),
		[](const int &value) {
			return &value;
		}), 2);
	const std::list<int> iterator_projected {1, 2, 3};
	RIWO_TEST_CHECK_EQ(riwo::mean(iterator_projected.begin(), iterator_projected.end(),
		[](std::list<int>::const_iterator it) {
			return &*it;
		}), 2);

	const riwo::string_deque deque {"a", "b", "c"};
	RIWO_TEST_CHECK_EQ(deque.join(1, "::"), "b::c");
	const riwo::string_list list {"left", "right"};
	RIWO_TEST_CHECK_EQ(list.join('/'), "left/right");
	const riwo::string_set set {"beta", "alpha"};
	RIWO_TEST_CHECK_EQ(set.join(','), "alpha,beta");
}

void optional_expected_and_async_helpers()
{
	auto optional = riwo::make_optional(std::string("value"));
	RIWO_TEST_CHECK_EQ(optional.and_then([](const std::string &value) {
		return riwo::optional<size_t>(value.size());
	}).value_or(0), 5U);
	RIWO_TEST_CHECK_EQ(riwo::optional<int>().or_else(42).value_or(0), 42);

	riwo::expected<int,std::string> value(21);
	RIWO_TEST_CHECK_EQ(value.transform([](int number) { return number * 2; }).value_or(), 42);
	value.despair("failed");
	RIWO_TEST_CHECK_EQ(value.or_else(7).value_or(), 7);
	RIWO_TEST_CHECK_EQ(value.or_else([](const std::string &error) {
		return riwo::expected<int,std::string>(static_cast<int>(error.size()));
	}).value_or(), 6);

	riwo::expected<void,std::string> no_value;
	RIWO_TEST_CHECK(no_value);
	no_value.despair("error");
	RIWO_TEST_CHECK(no_value.is_error());
	no_value.emplace();
	RIWO_TEST_CHECK(no_value.has_value());

	riwo::error_code error;
	RIWO_TEST_CHECK_EQ(riwo::expected_value_or_error(
		riwo::sys_expected<int>(9), error), 9);
	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(riwo::expected_value_or_error(
		riwo::sys_expected<int>(riwo::sys_unexpected(
			std::make_error_code(std::errc::invalid_argument))), error), 0);
	RIWO_TEST_CHECK(error == std::errc::invalid_argument);
	RIWO_TEST_CHECK_THROWS(riwo::expected_value_or_throw(
		riwo::sys_expected<int>(riwo::sys_unexpected(error))), std::system_error);

	std::string text = "borrowed";
	auto borrowed = riwo::capture_async_argument(std::ref(text));
	RIWO_TEST_CHECK(&riwo::unwrap_async_argument(borrowed) == &text);
	auto owned = riwo::capture_async_argument(std::string("owned"));
	RIWO_TEST_CHECK_EQ(riwo::unwrap_async_argument(owned), "owned");
}

void formatting_and_endpoints()
{
	const riwo::optional<int> empty;
	RIWO_TEST_CHECK_EQ(std::format("{}", empty), "optional(null)");
	RIWO_TEST_CHECK_EQ(std::format("{}", riwo::optional<int>(42)), "42");
	RIWO_TEST_CHECK_EQ(std::format("{}", std::atomic_int(7)), "7");
	RIWO_TEST_CHECK_EQ(std::format("{}", std::filesystem::path("a/b")), "a/b");
	RIWO_TEST_CHECK_EQ(std::format("{}", std::vector<std::byte> {}),
		"empty[std::vector<std::byte>]");
	RIWO_TEST_CHECK_EQ(std::format("{}", std::make_pair(1, 2)), "'1'-'2'");

	riwo::tcp_endpoint_wrapper loopback(riwo::ip_type::loopback, 8080);
	RIWO_TEST_CHECK(loopback->address().is_loopback());
	RIWO_TEST_CHECK_EQ(loopback->port(), uint16_t {8080});
	RIWO_TEST_CHECK_EQ(std::format("{}", *loopback), "127.0.0.1:8080");

	const riwo::udp_endpoint_wrapper ipv6(riwo::ip_type::v6, 53);
	const asio::ip::udp::endpoint &endpoint = ipv6;
	RIWO_TEST_CHECK(endpoint.address().is_v6());
	RIWO_TEST_CHECK_EQ(endpoint.port(), uint16_t {53});
}

void application_environment()
{
	using namespace riwo::app::literals;
	const auto original = riwo::app::current_directory();
	RIWO_TEST_CHECK(original);
	riwo::test::temporary_directory directory;
	const bool changed = bool(riwo::app::set_current_directory(directory.path()));
	const auto current = riwo::app::current_directory();
	std::error_code equivalent_error;
	const bool current_matches = changed and current and std::filesystem::equivalent(
		*current, directory.path(), equivalent_error);
	if( not current_matches )
	{
		std::cerr << "current directory mismatch: changed=" << changed
			<< ", current='" << (current ? current->string() : "<error>")
			<< "', expected='" << directory.path().string()
			<< "', equivalent_error='" << equivalent_error.message() << "'\n";
	}
	const bool absolute_matches = "relative.txt"_abs ==
		riwo::app::dir_path().value() / "relative.txt";
	RIWO_TEST_CHECK(riwo::app::set_current_directory(*original));
	RIWO_TEST_CHECK(changed);
	RIWO_TEST_CHECK(current_matches);
	RIWO_TEST_CHECK(absolute_matches);

	constexpr std::string_view key = "RIWO_TEST_PUBLIC_API_ENV";
	RIWO_TEST_CHECK(riwo::app::setenv(key, "first"));
	RIWO_TEST_CHECK(riwo::app::setenv(key, "second", false));
	RIWO_TEST_CHECK_EQ(riwo::app::getenv(key).value_or(""), "first");
	RIWO_TEST_CHECK(riwo::app::getenvs()->contains(std::string(key)));
	RIWO_TEST_CHECK(riwo::app::unsetenv(key));
	RIWO_TEST_CHECK(riwo::app::current_user());
	RIWO_TEST_CHECK(riwo::app::home_directory());
	riwo::none_instruction();
}

std::filesystem::path fixture_path;

void dynamic_library()
{
	riwo::library missing(fixture_path.string() + ".missing");
	auto missing_result = missing.load();
	RIWO_TEST_CHECK(not missing_result);
	RIWO_TEST_CHECK(missing_result.error() == std::errc::no_such_file_or_directory);

	riwo::library plugin(fixture_path);
	RIWO_TEST_CHECK(plugin.load());
	RIWO_TEST_CHECK(plugin.load());
	RIWO_TEST_CHECK(plugin.is_loaded());
	RIWO_TEST_CHECK(plugin.exists("riwo_test_twice"));
	RIWO_TEST_CHECK(not plugin.exists("riwo_test_missing"));
	auto twice = plugin.interface<int(int)>("riwo_test_{}", "twice");
	RIWO_TEST_CHECK(twice);
	RIWO_TEST_CHECK_EQ((*twice)(21), 42);

	riwo::library moved(std::move(plugin));
	RIWO_TEST_CHECK(moved.is_loaded());
	RIWO_TEST_CHECK(not plugin.is_loaded());
	RIWO_TEST_CHECK(moved.unload());
	RIWO_TEST_CHECK(moved.is_loaded());
	riwo::library assigned(fixture_path.string() + ".missing");
	assigned = std::move(moved);
	RIWO_TEST_CHECK(assigned.is_loaded());
	RIWO_TEST_CHECK(not moved.is_loaded());
	RIWO_TEST_CHECK(assigned.unload());
	RIWO_TEST_CHECK(not assigned.is_loaded());
	RIWO_TEST_CHECK(assigned.unload());
}

} //namespace

int main(int argc, const char *const argv[])
{
	if( argc < 2 )
		return 2;
	fixture_path = std::filesystem::absolute(argv[1]);
	// Treat the required fixture path as argv[0] for the shared test parser so
	// optional --case/--repeat/--seed arguments can follow it.
	return riwo::test::run(argc - 1, argv + 1, {
		{"flags and parameters", flags_and_parameters},
		{"value and string algorithms", value_and_string_algorithms},
		{"optional, expected, and async helpers", optional_expected_and_async_helpers},
		{"formatting and endpoints", formatting_and_endpoints},
		{"application environment", application_environment},
		{"dynamic library", dynamic_library},
	});
}
