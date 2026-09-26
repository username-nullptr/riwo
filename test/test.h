// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_TEST_TEST_H
#define RIWO_TEST_TEST_H

#include <exception>
#include <atomic>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace riwo::test
{

template <typename T>
concept canonical_executor_type = requires {
	typename T::executor_type;
	typename T::executor_t;
} and std::same_as<typename T::executor_type,typename T::executor_t>;

class temporary_directory
{
public:
	temporary_directory()
	{
		static std::atomic_uint64_t sequence {0};
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		m_path = std::filesystem::temp_directory_path() /
			("riwo-test-" + std::to_string(stamp) + '-' +
			 std::to_string(sequence.fetch_add(1)));
		std::filesystem::create_directories(m_path);
	}

	~temporary_directory()
	{
		std::error_code error;
		std::filesystem::remove_all(m_path, error);
	}

	temporary_directory(const temporary_directory&) = delete;
	temporary_directory &operator=(const temporary_directory&) = delete;

	[[nodiscard]] const std::filesystem::path &path() const noexcept
	{
		return m_path;
	}

private:
	std::filesystem::path m_path;
};

struct test_case
{
	std::string_view name;
	void (*function)();
};

struct run_context
{
	std::string_view test_name {};
	size_t iteration = 1;
	size_t repeat = 1;
	std::uint64_t seed = 0;
};

inline thread_local run_context active_run_context {};

[[nodiscard]] inline const run_context &current_run() noexcept
{
	return active_run_context;
}

[[nodiscard]] inline std::uint64_t current_seed() noexcept
{
	return active_run_context.seed;
}

// Small deterministic generator for scheduling perturbations in stress tests.
class random_sequence
{
public:
	explicit random_sequence(std::uint64_t seed = current_seed()) noexcept :
		m_state(seed != 0 ? seed : 0x9e3779b97f4a7c15ULL) {}

	[[nodiscard]] std::uint64_t next() noexcept
	{
		m_state += 0x9e3779b97f4a7c15ULL;
		auto value = m_state;
		value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
		value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
		return value ^ (value >> 31);
	}

	[[nodiscard]] size_t bounded(size_t upper_bound) noexcept
	{
		return upper_bound == 0 ? 0 : static_cast<size_t>(next() % upper_bound);
	}

private:
	std::uint64_t m_state;
};

[[noreturn]] inline void fail (
	std::string_view expression,
	const std::source_location &location = std::source_location::current()
)
{
	throw std::runtime_error (
		std::string(location.file_name()) + ':' + std::to_string(location.line()) +
		": check failed: " + std::string(expression)
	);
}

inline void check(
	bool condition,
	std::string_view expression,
	const std::source_location &location = std::source_location::current()
)
{
	if( not condition )
		fail(expression, location);
}

template <typename Actual, typename Expected>
inline void check_equal(
	const Actual &actual,
	const Expected &expected,
	std::string_view actual_expression,
	std::string_view expected_expression,
	const std::source_location &location = std::source_location::current()
)
{
	const bool equal = [&]
	{
		if constexpr(std::integral<Actual> and std::integral<Expected> and
			not std::same_as<Actual,bool> and not std::same_as<Expected,bool> and
			(std::is_signed_v<Actual> != std::is_signed_v<Expected>))
		{
			if constexpr(std::is_signed_v<Actual>)
				return actual < 0 ? false :
					static_cast<std::make_unsigned_t<Actual>>(actual) == expected;
			else
				return expected < 0 ? false :
					actual == static_cast<std::make_unsigned_t<Expected>>(expected);
		}
		else
			return actual == expected;
	}();
	if(equal)
		return;
	std::ostringstream message;
	message << actual_expression << " == " << expected_expression;
	if constexpr(requires { message << actual << expected; })
		message << " (actual: " << actual << ", expected: " << expected << ')';
	fail(message.str(), location);
}

namespace detail
{

inline std::atomic<const test_case*> terminating_test {nullptr};
inline std::atomic_size_t terminating_iteration {0};
inline std::atomic_size_t terminating_repeat {0};
inline std::atomic_uint64_t terminating_seed {0};

[[noreturn]] inline void terminate_with_diagnostics() noexcept
{
	const auto *test = terminating_test.load(std::memory_order_acquire);
	const auto thread = std::hash<std::thread::id> {}(std::this_thread::get_id());
	if( test )
	{
		std::fprintf(stderr,
			"[TERMINATE] %.*s (iteration %zu/%zu, seed %llu, thread %zu)\n",
			static_cast<int>(test->name.size()), test->name.data(),
			terminating_iteration.load(std::memory_order_relaxed),
			terminating_repeat.load(std::memory_order_relaxed),
			static_cast<unsigned long long>(
				terminating_seed.load(std::memory_order_relaxed)),
			thread
		);
	}
	else
		std::fprintf(stderr, "[TERMINATE] outside a test case (thread %zu)\n", thread);

	if( const auto exception = std::current_exception() )
	{
		try {
			std::rethrow_exception(exception);
		}
		catch(const std::exception &error) {
			std::fprintf(stderr, "[TERMINATE] active exception: %s\n", error.what());
		}
		catch(...) {
			std::fprintf(stderr, "[TERMINATE] active non-standard exception\n");
		}
	}
	std::fflush(stderr);
	std::abort();
}

struct run_options
{
	std::vector<std::string_view> cases;
	size_t repeat = 1;
	std::uint64_t seed = 0;
	bool list = false;
	bool fail_fast = false;
	bool help = false;
};

template <typename Integer>
[[nodiscard]] Integer parse_integer(std::string_view text, std::string_view option)
{
	Integer value {};
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
	if(result.ec != std::errc {} or result.ptr != text.data() + text.size())
		throw std::invalid_argument(std::string(option) + " requires an integer");
	return value;
}

[[nodiscard]] inline run_options environment_options()
{
	run_options options;
	options.seed = static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	if(const auto *value = std::getenv("RIWO_TEST_CASE"); value and *value)
		options.cases.emplace_back(value);
	if(const auto *value = std::getenv("RIWO_TEST_REPEAT"); value and *value)
	{
		options.repeat = parse_integer<size_t>(value, "RIWO_TEST_REPEAT");
		if(options.repeat == 0)
			throw std::invalid_argument("RIWO_TEST_REPEAT must be positive");
	}
	if(const auto *value = std::getenv("RIWO_TEST_SEED"); value and *value)
		options.seed = parse_integer<std::uint64_t>(value, "RIWO_TEST_SEED");
	if(const auto *value = std::getenv("RIWO_TEST_FAIL_FAST"); value and *value)
		options.fail_fast = std::string_view(value) != "0";
	return options;
}

[[nodiscard]] inline run_options parse_options(int argc, const char *const argv[])
{
	auto options = environment_options();
	for(int index = 1; index < argc; ++index)
	{
		const std::string_view argument(argv[index]);
		auto value_after = [&](std::string_view option) -> std::string_view
		{
			if(index + 1 >= argc)
				throw std::invalid_argument(std::string(option) + " requires a value");
			return argv[++index];
		};

		if(argument == "--case")
			options.cases.emplace_back(value_after(argument));
		else if(argument == "--repeat")
		{
			options.repeat = parse_integer<size_t>(value_after(argument), argument);
			if(options.repeat == 0)
				throw std::invalid_argument("--repeat must be positive");
		}
		else if(argument == "--seed")
			options.seed = parse_integer<std::uint64_t>(value_after(argument), argument);
		else if(argument == "--list")
			options.list = true;
		else if(argument == "--fail-fast")
			options.fail_fast = true;
		else if(argument == "--help" or argument == "-h")
			options.help = true;
		else
			throw std::invalid_argument("unknown option: " + std::string(argument));
	}
	return options;
}

[[nodiscard]] inline std::uint64_t hash_name(std::string_view name) noexcept
{
	std::uint64_t hash = 1469598103934665603ULL;
	for(const auto value : name)
	{
		hash ^= static_cast<unsigned char>(value);
		hash *= 1099511628211ULL;
	}
	return hash;
}

[[nodiscard]] inline std::uint64_t iteration_seed(
	std::uint64_t base, std::string_view name, size_t iteration) noexcept
{
	random_sequence sequence(base ^ hash_name(name) ^
		(static_cast<std::uint64_t>(iteration) * 0x9e3779b97f4a7c15ULL));
	return sequence.next();
}

[[nodiscard]] inline bool selected(
	std::string_view name, const std::vector<std::string_view> &cases) noexcept
{
	if(cases.empty())
		return true;
	for(const auto selected_case : cases)
	{
		if(name == selected_case)
			return true;
	}
	return false;
}

inline void print_help()
{
	std::cout
		<< "Options:\n"
		<< "  --list              list test cases without running them\n"
		<< "  --case <name>       run one named case; may be repeated\n"
		<< "  --repeat <count>    recreate and run each selected case count times\n"
		<< "  --seed <value>      reproduce scheduling perturbations\n"
		<< "  --fail-fast         stop after the first failed iteration\n";
}

inline int run_with_options(
	std::initializer_list<test_case> tests, const run_options &options)
{
	std::set_terminate(terminate_with_diagnostics);
	if(options.help)
	{
		print_help();
		return 0;
	}
	if(options.list)
	{
		for(const auto &test : tests)
			std::cout << test.name << '\n';
		return 0;
	}

	size_t selected_count = 0;
	for(const auto &test : tests)
		selected_count += selected(test.name, options.cases);
	if(selected_count == 0)
	{
		std::cerr << "no test case matched";
		if(not options.cases.empty())
			std::cerr << ": " << options.cases.front();
		std::cerr << '\n';
		return 2;
	}

	std::cout << "[CONFIG] repeat=" << options.repeat
		<< " seed=" << options.seed << '\n' << std::flush;
	size_t failures = 0;
	size_t completed = 0;
	const auto total = selected_count * options.repeat;
	for(const auto &test : tests)
	{
		if(not selected(test.name, options.cases))
			continue;
		for(size_t iteration = 1; iteration <= options.repeat; ++iteration)
		{
			active_run_context = {
				test.name,
				iteration,
				options.repeat,
				iteration_seed(options.seed, test.name, iteration)
			};
			terminating_iteration.store(iteration, std::memory_order_relaxed);
			terminating_repeat.store(options.repeat, std::memory_order_relaxed);
			terminating_seed.store(active_run_context.seed, std::memory_order_relaxed);
			terminating_test.store(&test, std::memory_order_release);
			std::cout << "[RUN] " << test.name << " (iteration " << iteration
				<< '/' << options.repeat << ", seed " << active_run_context.seed
				<< ")\n" << std::flush;
			try
			{
				const auto begin = std::chrono::steady_clock::now();
				test.function();
				const auto elapsed = std::chrono::duration<double,std::milli>(
					std::chrono::steady_clock::now() - begin).count();
				std::cout << "[PASS] " << test.name;
				if(options.repeat != 1)
					std::cout << " (iteration " << iteration << '/' << options.repeat
						<< ", seed " << active_run_context.seed << ')';
				std::cout << " [" << elapsed << " ms]\n";
			}
			catch(const std::exception &error)
			{
				failures++;
				std::cerr << "[FAIL] " << test.name << " (iteration " << iteration
					<< '/' << options.repeat << ", seed " << active_run_context.seed
					<< "): " << error.what() << '\n';
			}
			catch(...)
			{
				failures++;
				std::cerr << "[FAIL] " << test.name << " (iteration " << iteration
					<< '/' << options.repeat << ", seed " << active_run_context.seed
					<< "): unknown exception\n";
			}
			terminating_test.store(nullptr, std::memory_order_release);
			completed++;
			if(failures != 0 and options.fail_fast)
				break;
		}
		if(failures != 0 and options.fail_fast)
			break;
	}
	terminating_test.store(nullptr, std::memory_order_release);
	active_run_context = {};

	std::cout << (completed - failures) << '/' << completed
		<< (total == selected_count ? " tests passed\n" : " runs passed\n");
	return failures == 0 ? 0 : 1;
}

} //namespace detail

inline int run(std::initializer_list<test_case> tests)
{
	try
	{
		return detail::run_with_options(tests, detail::environment_options());
	}
	catch(const std::exception &error)
	{
		std::cerr << "test option error: " << error.what() << '\n';
		return 2;
	}
}

inline int run(
	int argc, const char *const argv[], std::initializer_list<test_case> tests)
	noexcept
{
	try
	{
		return detail::run_with_options(tests, detail::parse_options(argc, argv));
	}
	catch(const std::exception &error)
	{
		std::cerr << "test option error: " << error.what() << '\n';
		return 2;
	}
}

template <typename Exception, typename Function>
inline void check_throws(
	Function &&function,
	std::string_view expression,
	const std::source_location &location = std::source_location::current()
)
{
	try
	{
		std::forward<Function>(function)();
	}
	catch(const Exception&)
	{
		return;
	}
	catch(...)
	{
		fail(std::string("unexpected exception type from: ") +
			std::string(expression), location);
	}
	fail(std::string("expected exception from: ") + std::string(expression), location);
}

} //namespace riwo::test

#define RIWO_TEST_CHECK(expression) \
	do { \
		::riwo::test::check(static_cast<bool>(expression), #expression); \
	} while(false)

#define RIWO_TEST_CHECK_EQ(actual, expected) \
	do { \
		::riwo::test::check_equal((actual), (expected), #actual, #expected); \
	} while(false)

#define RIWO_TEST_CHECK_THROWS(expression, exception_type) \
	do { \
		::riwo::test::check_throws<exception_type>( \
			[&] { static_cast<void>(expression); }, #expression, \
			std::source_location::current() \
		); \
	} while(false)

#endif //RIWO_TEST_TEST_H
