// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/crypto.h>

namespace
{

void sha1_vectors()
{
	riwo::crypto::sha1 empty;
	RIWO_TEST_CHECK_EQ(
		empty.finalize().hex(false),
		"da39a3ee5e6b4b0d3255bfef95601890afd80709"
	);

	riwo::crypto::sha1 abc;
	abc.append('a').append("bc").finalize();
	RIWO_TEST_CHECK_EQ(
		abc.hex(),
		"A9993E364706816ABA3E25717850C26C9CD0D89D"
	);
	RIWO_TEST_CHECK_EQ(abc.base64(), "qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");

	riwo::crypto::sha1 original(std::string(80, 'x'));
	riwo::crypto::sha1 copied(original);
	riwo::crypto::sha1 assigned;
	assigned = original;
	original.append("original suffix").finalize();
	copied.append("original suffix").finalize();
	assigned.append("original suffix").finalize();
	RIWO_TEST_CHECK_EQ(copied.hex(), original.hex());
	RIWO_TEST_CHECK_EQ(assigned.hex(), original.hex());
}

void base64_and_secure_random()
{
	RIWO_TEST_CHECK_EQ(riwo::crypto::base64_encode(""), "");
	RIWO_TEST_CHECK_EQ(riwo::crypto::base64_encode("f"), "Zg==");
	RIWO_TEST_CHECK_EQ(riwo::crypto::base64_encode("fo"), "Zm8=");
	RIWO_TEST_CHECK_EQ(riwo::crypto::base64_encode("foo"), "Zm9v");

	const std::array binary {std::byte {0xFF}, std::byte {0xEE}};
	RIWO_TEST_CHECK_EQ(riwo::crypto::base64_encode(binary), "/+4=");
	RIWO_TEST_CHECK(riwo::crypto::secure_random_bytes(riwo::mutable_buffer {}));
	RIWO_TEST_CHECK(not riwo::crypto::secure_random_bytes(
		riwo::mutable_buffer(nullptr, 1)));

	std::array<std::byte,32> random {};
	RIWO_TEST_CHECK(riwo::crypto::secure_random_bytes(asio::buffer(random)));
}

void uuid_values()
{
	const riwo::crypto::uuid dns_namespace("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
	RIWO_TEST_CHECK(dns_namespace.is_valid());
	RIWO_TEST_CHECK_EQ(
		dns_namespace.to_string(),
		"6BA7B810-9DAD-11D1-80B4-00C04FD430C8"
	);
	RIWO_TEST_CHECK_EQ(
		dns_namespace.to_string(true),
		"{6BA7B810-9DAD-11D1-80B4-00C04FD430C8}"
	);

	const auto named = riwo::crypto::uuid::generate_v5(dns_namespace, "www.widgets.com");
	RIWO_TEST_CHECK(named.is_valid());
	RIWO_TEST_CHECK_EQ(named.version(), riwo::crypto::uuid_version::v5);
	RIWO_TEST_CHECK_EQ(
		named.to_string(),
		"21F7F8DE-8051-5B89-8680-0195EF798B6A"
	);

	const riwo::crypto::uuid invalid("not-a-uuid");
	RIWO_TEST_CHECK(not invalid.is_valid());
	RIWO_TEST_CHECK_EQ(invalid.version(), riwo::crypto::uuid_version::none);
	RIWO_TEST_CHECK(invalid.to_string().empty());

	const riwo::crypto::uuid nil("00000000-0000-0000-0000-000000000000");
	RIWO_TEST_CHECK(nil.is_valid());
	RIWO_TEST_CHECK(nil.is_nil());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"SHA-1 vectors", sha1_vectors},
		{"Base64 and secure random", base64_and_secure_random},
		{"UUID values", uuid_values},
	});
}
