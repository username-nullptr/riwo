// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/algorithm/math.h>
#include <riwo/core/algorithm/misc.h>
#include <riwo/core/algorithm/sha1.h>
#include <riwo/core/algorithm/uuid.h>
#include <riwo/core/async_expected.h>
#include <riwo/core/cxx/expected.h>
#include <riwo/core/cxx/optional.h>
#include <riwo/core/cxx/tools.h>
#include <riwo/core/shared_mutex.h>
#include <riwo/core/atomic_mutex.h>
#include <riwo/core/utils/byte_order.h>
#include <riwo/core/utils/streamer.h>
#include <riwo/core/utils/string_tools.h>
#include <memory>

namespace
{

template <typename Buffer, typename Source>
concept buffer_data_copyable = requires(Source &&source) {
	riwo::copy_buffer_data<Buffer>(std::forward<Source>(source));
};

static_assert(riwo::is_array_buffer_v<std::array<std::uint32_t,4>>);
static_assert(riwo::is_vector_buffer_v<std::vector<std::uint32_t>>);
static_assert(riwo::is_string_buffer_v<std::string>);

static_assert(not riwo::is_array_buffer_v<std::array<std::string,4>>);
static_assert(not riwo::is_array_buffer_v<std::array<const std::uint32_t,4>>);
static_assert(not riwo::is_vector_buffer_v<std::vector<std::string>>);
static_assert(not riwo::is_vector_buffer_v<std::vector<bool>>);
static_assert(not riwo::is_buffer_v<std::vector<std::string>>);

static_assert(std::derived_from<riwo::optional<int>,std::optional<int>>);
static_assert(std::same_as<riwo::nullopt_t,std::nullopt_t>);
static_assert(riwo::is_optional_v<riwo::optional<int>>);
static_assert(riwo::is_optional_v<std::optional<int>>);
static_assert(std::same_as <
	decltype(riwo::make_optional("value")),riwo::optional<const char*>
>);
static_assert(std::same_as <
	decltype(std::declval<riwo::optional<std::string>&>().emplace(3, 'x')),
	std::string&
>);
static_assert(std::same_as <
	decltype(std::declval<riwo::optional<int>&>().reset()),void
>);
static_assert(std::same_as <
	decltype(std::declval<riwo::expected<int,std::string>&>().emplace(1)),int&
>);
static_assert(std::same_as <
	decltype(std::declval<riwo::expected<void,std::string>&>().emplace()),void
>);
#if RIWO_HAS_STD_EXPECTED
static_assert(std::derived_from <
	riwo::expected<int,std::string>,std::expected<int,std::string>
>);
static_assert(std::derived_from <
	riwo::unexpected<std::string>,std::unexpected<std::string>
>);
static_assert(riwo::is_expected_v<std::expected<int,std::string>>);
#endif

static_assert(buffer_data_copyable<
	std::vector<std::uint32_t>, std::vector<std::byte>
>);
static_assert(not buffer_data_copyable<
	std::vector<std::string>, std::vector<std::byte>
>);
static_assert(not buffer_data_copyable<
	std::vector<std::byte>, std::vector<std::string>
>);

struct polymorphic_base
{
	virtual ~polymorphic_base() = default;
};

struct polymorphic_derived final : polymorphic_base {};

struct meta_fields_sample
{
	RIWO_META_FIELDS (
		( int, plain ),
		( std::string, initialized, "value" ),
		( (std::pair<int,int>), pair )
	);
};

struct large_meta_fields_sample
{
	RIWO_META_FIELDS (
		( int, field_00 ),
		( int, field_01 ),
		( int, field_02 ),
		( int, field_03 ),
		( int, field_04 ),
		( int, field_05 ),
		( int, field_06 ),
		( int, field_07 ),
		( int, field_08 ),
		( int, field_09 ),
		( int, field_10 ),
		( int, field_11 ),
		( int, field_12 ),
		( int, field_13 ),
		( int, field_14 ),
		( int, field_15 ),
		( int, field_16 ),
		( int, field_17 ),
		( int, field_18 ),
		( int, field_19 ),
		( int, field_20 ),
		( int, field_21 ),
		( int, field_22 ),
		( int, field_23 ),
		( int, field_24 ),
		( int, field_25 ),
		( int, field_26 ),
		( int, field_27 ),
		( int, field_28 ),
		( int, field_29 ),
		( int, field_30 ),
		( int, field_31 ),
		( int, field_32 ),
		( int, field_33 ),
		( int, field_34 ),
		( int, field_35 ),
		( int, field_36 ),
		( int, field_37 ),
		( int, field_38 ),
		( int, field_39 ),
		( int, field_40 ),
		( int, field_41 ),
		( int, field_42 ),
		( int, field_43 ),
		( int, field_44 ),
		( int, field_45 ),
		( int, field_46 ),
		( int, field_47 ),
		( int, field_48 ),
		( int, field_49 ),
		( int, field_50 ),
		( int, field_51 ),
		( int, field_52 ),
		( int, field_53 ),
		( int, field_54 ),
		( int, field_55 ),
		( int, field_56 ),
		( int, field_57 ),
		( int, field_58 ),
		( int, field_59 ),
		( int, field_60 ),
		( int, field_61 ),
		( int, field_62 ),
		( int, field_63 ),
		( int, field_64 ),
		( int, field_65 ),
		( int, field_66 ),
		( int, field_67 ),
		( int, field_68 ),
		( int, field_69 ),
		( int, field_70 ),
		( int, field_71 ),
		( int, field_72 ),
		( int, field_73 ),
		( int, field_74 ),
		( int, field_75 ),
		( int, field_76 ),
		( int, field_77 ),
		( int, field_78 ),
		( int, field_79 )
	);
};

void meta_fields_macros()
{
	meta_fields_sample sample;
	RIWO_TEST_CHECK_EQ(sample.plain, 0);
	RIWO_TEST_CHECK_EQ(sample.initialized, "value");
	RIWO_TEST_CHECK_EQ(sample.pair, (std::pair<int,int> {}));

	auto fields = sample.meta_fields();
	static_assert(std::tuple_size_v<decltype(fields)> == 3);
	std::get<0>(fields) = 42;
	RIWO_TEST_CHECK_EQ(sample.plain, 42);

	large_meta_fields_sample large;
	auto large_fields = large.meta_fields();
	static_assert(std::tuple_size_v<decltype(large_fields)> == 80);
	std::get<79>(large_fields) = 79;
	RIWO_TEST_CHECK_EQ(large.field_79, 79);
}

void type_names()
{
	const char *first = riwo::type_name<int>();
	RIWO_TEST_CHECK(first != nullptr);
	RIWO_TEST_CHECK(first == riwo::type_name<int>());
	RIWO_TEST_CHECK_EQ(std::string_view(first), "int");
	RIWO_TEST_CHECK_EQ(
		std::string_view(riwo::type_name(typeid(int))),
		std::string_view(first)
	);

	polymorphic_derived derived;
	polymorphic_base &base = derived;
	const char *dynamic = riwo::type_name(base);
	RIWO_TEST_CHECK(dynamic != nullptr);
	RIWO_TEST_CHECK(dynamic == riwo::type_name(base));
	RIWO_TEST_CHECK_EQ(
		std::string_view(dynamic),
		std::string_view(riwo::type_name<polymorphic_derived>())
	);
}

void percent_encoding()
{
	RIWO_TEST_CHECK_EQ(
		riwo::to_percent_encoding("a b/c?value=1"),
		"a%20b%2Fc%3Fvalue%3D1"
	);
	RIWO_TEST_CHECK_EQ(
		riwo::to_percent_encoding("a b/c", "/"),
		"a%20b/c"
	);
	RIWO_TEST_CHECK_EQ(
		riwo::to_percent_encoding(
			"a-b", std::string_view(), std::string_view("-")
		),
		"a%2Db"
	);
	RIWO_TEST_CHECK_EQ(
		riwo::from_percent_encoding(std::string("a%20b%2fc")),
		"a b/c"
	);
	RIWO_TEST_CHECK_EQ(
		riwo::from_percent_encoding(std::wstring(L"%E4%B8%AD")),
		std::wstring(L"\x00E4\x00B8\x00AD")
	);
}

void wildcard_matching()
{
	RIWO_TEST_CHECK_EQ(riwo::wildcard_match("README.md", "README.md"), 0);
	RIWO_TEST_CHECK(riwo::wildcard_match("*.md", "README.md") > 0);
	RIWO_TEST_CHECK(riwo::wildcard_match("riwo/??re.h", "riwo/core.h") > 0);
	RIWO_TEST_CHECK_EQ(riwo::wildcard_match("*.cpp", "README.md"), -1);
	RIWO_TEST_CHECK_EQ(riwo::wildcard_match("", ""), 0);
}

void sha1_vectors()
{
	riwo::sha1 empty;
	RIWO_TEST_CHECK_EQ(
		empty.finalize().hex(false),
		"da39a3ee5e6b4b0d3255bfef95601890afd80709"
	);

	riwo::sha1 abc;
	abc.append('a').append("bc").finalize();
	RIWO_TEST_CHECK_EQ(
		abc.hex(),
		"A9993E364706816ABA3E25717850C26C9CD0D89D"
	);
	RIWO_TEST_CHECK_EQ(abc.base64(), "qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");

	riwo::sha1 original(std::string(80, 'x'));
	riwo::sha1 copied(original);
	riwo::sha1 assigned;
	assigned = original;
	original.append("original suffix").finalize();
	copied.append("original suffix").finalize();
	assigned.append("original suffix").finalize();
	RIWO_TEST_CHECK_EQ(copied.hex(), original.hex());
	RIWO_TEST_CHECK_EQ(assigned.hex(), original.hex());
}

void uuid_values()
{
	const riwo::uuid dns_namespace("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
	RIWO_TEST_CHECK(dns_namespace.is_valid());
	RIWO_TEST_CHECK_EQ(
		dns_namespace.to_string(),
		"6BA7B810-9DAD-11D1-80B4-00C04FD430C8"
	);
	RIWO_TEST_CHECK_EQ(
		dns_namespace.to_string(true),
		"{6BA7B810-9DAD-11D1-80B4-00C04FD430C8}"
	);

	const auto named = riwo::uuid::generate_v5(dns_namespace, "www.widgets.com");
	RIWO_TEST_CHECK(named.is_valid());
	RIWO_TEST_CHECK_EQ(named.version(), riwo::uuid_version::v5);
	RIWO_TEST_CHECK_EQ(
		named.to_string(),
		"21F7F8DE-8051-5B89-8680-0195EF798B6A"
	);

	const riwo::uuid invalid("not-a-uuid");
	RIWO_TEST_CHECK(not invalid.is_valid());
	RIWO_TEST_CHECK_EQ(invalid.version(), riwo::uuid_version::none);
	RIWO_TEST_CHECK(invalid.to_string().empty());

	const riwo::uuid nil("00000000-0000-0000-0000-000000000000");
	RIWO_TEST_CHECK(nil.is_valid());
	RIWO_TEST_CHECK(nil.is_nil());
}

void arithmetic_and_byte_order()
{
	const std::array values {2, 4, 6, 8};
	RIWO_TEST_CHECK_EQ(riwo::mean(values.begin(), values.end()), 5);

	constexpr std::uint32_t value = 0x01020304U;
	RIWO_TEST_CHECK_EQ(riwo::reverse(value), 0x04030201U);
	RIWO_TEST_CHECK_EQ(riwo::reverse(riwo::reverse(value)), value);
	RIWO_TEST_CHECK(riwo::is_little_endian() != riwo::is_big_endian());
	RIWO_TEST_CHECK_EQ(riwo::ntoh(riwo::hton(value)), value);
}

void string_tools()
{
	RIWO_TEST_CHECK_EQ(riwo::strtls::trimmed(" \t Riwo \r\n"), "Riwo");
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_lower("RiWo"), "riwo");
	RIWO_TEST_CHECK_EQ(riwo::strtls::to_upper(std::string("RiWo")), "RIWO");
	RIWO_TEST_CHECK(riwo::strtls::is_alpha("Riwo"));
	RIWO_TEST_CHECK(not riwo::strtls::is_alpha("Riwo1"));
	RIWO_TEST_CHECK(riwo::strtls::is_digit("012345"));
	RIWO_TEST_CHECK_EQ(*riwo::strtls::to_int32("-42"), -42);
	RIWO_TEST_CHECK_EQ(*riwo::strtls::to_uint32("2a", 16), 42U);
	RIWO_TEST_CHECK(not riwo::strtls::to_int32("12x"));
	RIWO_TEST_CHECK_EQ(riwo::strtls::file_name("/tmp/riwo/test.cpp"), "test.cpp");
	RIWO_TEST_CHECK_EQ(riwo::strtls::file_path("/tmp/riwo/test.cpp"), "/tmp/riwo/");
}

void buffer_copying()
{
	const std::vector<std::byte> source {
		std::byte {0x01}, std::byte {0x02}, std::byte {0x03},
		std::byte {0x04}, std::byte {0x05}
	};
	const auto words = riwo::copy_buffer_data<std::vector<std::uint16_t>>(source);
	RIWO_TEST_CHECK_EQ(words.size(), 3U);
	RIWO_TEST_CHECK(std::memcmp(words.data(), source.data(), source.size()) == 0);

	const auto text = riwo::copy_buffer_data<std::string>(source);
	RIWO_TEST_CHECK_EQ(text.size(), source.size());
	RIWO_TEST_CHECK(std::memcmp(text.data(), source.data(), source.size()) == 0);
}

void optional_and_expected()
{
	riwo::optional<std::string> optional;
	RIWO_TEST_CHECK(not optional);
	RIWO_TEST_CHECK_EQ(optional.value_or("fallback"), "fallback");
	RIWO_TEST_CHECK(optional == riwo::nullopt);
	RIWO_TEST_CHECK(optional == riwo::optional<std::string> {});
	RIWO_TEST_CHECK_THROWS(optional.value(), riwo::bad_optional_access);

	optional.emplace(3, 'x');
	RIWO_TEST_CHECK(optional.has_value());
	RIWO_TEST_CHECK_EQ(*optional, "xxx");
	const auto length = optional.transform([](const auto &value) {
		return value.size();
	});
	RIWO_TEST_CHECK_EQ(*length, size_t {3});

	std::optional<std::string> standard_optional("standard");
	optional = standard_optional;
	RIWO_TEST_CHECK_EQ(optional.value(), "standard");
	std::optional<std::string> &optional_base = optional;
	RIWO_TEST_CHECK_EQ(optional_base.value(), "standard");
	RIWO_TEST_CHECK_EQ(riwo::optional<int> {}.and_then([](int value) {
		return std::optional<long>(value);
	}), std::nullopt);
	RIWO_TEST_CHECK_EQ(riwo::optional<int> {}.or_else([] {
		return std::optional<int>(9);
	}).value(), 9);
	bool optional_recovered = false;
	static_cast<void>(riwo::optional<int> {}.or_else([&optional_recovered] {
		optional_recovered = true;
	}));
	RIWO_TEST_CHECK(optional_recovered);

	riwo::optional<std::unique_ptr<int>> move_source(
		std::in_place, std::make_unique<int>(1)
	);
	auto move_target = std::move(move_source);
	RIWO_TEST_CHECK(move_source.has_value());
	RIWO_TEST_CHECK(move_target.has_value());

	riwo::expected<int,std::string> success(42);
	RIWO_TEST_CHECK(success.has_value());
	RIWO_TEST_CHECK(not success.is_error());
	RIWO_TEST_CHECK_EQ(*success, 42);

	riwo::expected<int,std::string> error(
		riwo::unexpected<std::string>("failure")
	);
	RIWO_TEST_CHECK(not error.has_value());
	RIWO_TEST_CHECK(error.is_error());
	RIWO_TEST_CHECK_EQ(error.error(), "failure");
	RIWO_TEST_CHECK_EQ(error.error_or("fallback"), "failure");
	RIWO_TEST_CHECK_EQ(error.transform_error([](const std::string &message) {
		return message.size();
	}).error(), size_t {7});
	RIWO_TEST_CHECK_EQ(error.and_then([](int value) {
		return riwo::expected<long,std::string>(value);
	}).error(), "failure");
	RIWO_TEST_CHECK_THROWS(error.value(), riwo::bad_expected_access<std::string>);
	error.emplace(7);
	RIWO_TEST_CHECK_EQ(*error, 7);

	riwo::expected<void,std::string> no_value(riwo::unexpect, "void-error");
	RIWO_TEST_CHECK_EQ(no_value.transform([] { return 11; }).error(), "void-error");
	no_value.emplace();
	RIWO_TEST_CHECK(no_value.has_value());
	riwo::sys_expected<bool> direct_error {
		std::make_error_code(std::errc::invalid_argument)
	};
	RIWO_TEST_CHECK(not direct_error);
	RIWO_TEST_CHECK(direct_error.error() == std::errc::invalid_argument);

#if RIWO_HAS_STD_EXPECTED
	std::expected<int,std::string> &expected_base = success;
	RIWO_TEST_CHECK_EQ(expected_base.value(), 42);
	riwo::expected<int,std::string> from_standard(
		std::expected<int,std::string>(17)
	);
	RIWO_TEST_CHECK((from_standard == std::expected<int,std::string>(17)));
	auto standard_chain = riwo::expected<int,std::string>(
		riwo::unexpect, "standard-error"
	).and_then([](int value) {
		return std::expected<long,std::string>(value);
	});
	static_assert(std::same_as <
		decltype(standard_chain),std::expected<long,std::string>
	>);
	RIWO_TEST_CHECK_EQ(standard_chain.error(), "standard-error");
#endif
}

template <riwo::atomic_mutex_policy Policy>
void atomic_locks_with_policy()
{
	riwo::basic_atomic_mutex<Policy> mutex;
	RIWO_TEST_CHECK(mutex.try_lock());
	RIWO_TEST_CHECK(not mutex.try_lock());
	mutex.unlock();

	size_t count = 0;
	std::vector<std::thread> workers;
	for(size_t worker = 0; worker < 8; ++worker)
	{
		workers.emplace_back([&]
		{
			for(size_t index = 0; index < 5'000; ++index)
			{
				std::lock_guard lock(mutex);
				++count;
			}
		});
	}
	for(auto &worker : workers)
		worker.join();
	RIWO_TEST_CHECK_EQ(count, 40'000U);

	riwo::basic_atomic_shared_mutex<Policy> shared_mutex;
	size_t shared_value = 0;
	std::atomic_size_t writers_done {0};
	std::atomic_size_t try_writes {0};
	std::atomic_bool try_writer_done {false};
	std::atomic_bool invalid_read {false};
	workers.clear();
	for(size_t writer = 0; writer < 2; ++writer)
	{
		workers.emplace_back([&]
		{
			for(size_t index = 0; index < 5'000; ++index)
			{
				std::unique_lock lock(shared_mutex);
				++shared_value;
			}
			writers_done.fetch_add(1, std::memory_order_release);
		});
	}
	workers.emplace_back([&]
	{
		for(size_t index = 0; index < 50'000; ++index)
		{
			if( shared_mutex.try_lock() )
			{
				++shared_value;
				try_writes.fetch_add(1, std::memory_order_relaxed);
				shared_mutex.unlock();
			}
			else
				std::this_thread::yield();
		}
		try_writer_done.store(true, std::memory_order_release);
	});
	for(size_t reader = 0; reader < 4; ++reader)
	{
		workers.emplace_back([&]
		{
			size_t observed = 0;
			while( writers_done.load(std::memory_order_acquire) != 2 or
				not try_writer_done.load(std::memory_order_acquire) )
			{
				std::shared_lock lock(shared_mutex);
				if( shared_value < observed )
					invalid_read.store(true, std::memory_order_relaxed);
				observed = shared_value;
			}
		});
	}
	for(auto &worker : workers)
		worker.join();
	RIWO_TEST_CHECK(not invalid_read.load(std::memory_order_relaxed));
	RIWO_TEST_CHECK_EQ(shared_value,
		10'000U + try_writes.load(std::memory_order_relaxed));
	RIWO_TEST_CHECK(shared_mutex.try_lock_shared());
	RIWO_TEST_CHECK(not shared_mutex.try_lock());
	shared_mutex.unlock_shared();
	RIWO_TEST_CHECK(shared_mutex.try_lock());
	RIWO_TEST_CHECK(not shared_mutex.try_lock_shared());
	shared_mutex.unlock();

	shared_mutex.lock_shared();
	std::atomic_bool writer_started {false};
	std::atomic_bool writer_entered {false};
	std::thread blocked_writer([&]
	{
		writer_started.store(true, std::memory_order_release);
		writer_started.notify_one();
		std::unique_lock lock(shared_mutex);
		writer_entered.store(true, std::memory_order_release);
	});

	writer_started.wait(false, std::memory_order_acquire);
	bool writer_pending = false;
	const auto pending_deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while( std::chrono::steady_clock::now() < pending_deadline )
	{
		if( not shared_mutex.try_lock_shared() )
		{
			writer_pending = true;
			break;
		}
		shared_mutex.unlock_shared();
		std::this_thread::yield();
	}

	std::atomic_bool late_reader_entered {false};
	std::atomic_bool late_reader_saw_writer {false};
	std::thread late_reader;
	if( writer_pending )
	{
		late_reader = std::thread([&]
		{
			std::shared_lock lock(shared_mutex);
			late_reader_saw_writer.store(
				writer_entered.load(std::memory_order_acquire),
				std::memory_order_release
			);
			late_reader_entered.store(true, std::memory_order_release);
		});
	}

	shared_mutex.unlock_shared();
	blocked_writer.join();
	if( late_reader.joinable() )
		late_reader.join();

	RIWO_TEST_CHECK(writer_pending);
	RIWO_TEST_CHECK(writer_entered.load(std::memory_order_acquire));
	RIWO_TEST_CHECK(late_reader_entered.load(std::memory_order_acquire));
	RIWO_TEST_CHECK(late_reader_saw_writer.load(std::memory_order_acquire));
}

void atomic_locks()
{
	atomic_locks_with_policy<riwo::atomic_mutex_policy::balanced>();
	atomic_locks_with_policy<riwo::atomic_mutex_policy::low_latency>();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"meta fields macros", meta_fields_macros},
		{"type names", type_names},
		{"percent encoding", percent_encoding},
		{"wildcard matching", wildcard_matching},
		{"SHA-1 vectors", sha1_vectors},
		{"UUID values", uuid_values},
		{"arithmetic and byte order", arithmetic_and_byte_order},
		{"string tools", string_tools},
		{"buffer copying", buffer_copying},
		{"optional and expected", optional_and_expected},
		{"atomic locks", atomic_locks},
	});
}
