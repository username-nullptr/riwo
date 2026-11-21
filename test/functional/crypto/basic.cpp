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

void sha256_vectors()
{
	using riwo::crypto::sha256;
	RIWO_TEST_CHECK_EQ(sha256().hex(false),
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	sha256 abc("a");
	const auto prefix = abc.digest();
	abc.append("bc").finalize();
	RIWO_TEST_CHECK(prefix != abc.digest());
	RIWO_TEST_CHECK_EQ(abc.hex(),
		"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
	RIWO_TEST_CHECK_EQ(abc.finalize().base64(), "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
	RIWO_TEST_CHECK_THROWS(abc.append("d"), riwo::logic_error);

	constexpr std::string_view message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	RIWO_TEST_CHECK_EQ(sha256(message).hex(false),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	const std::string block(64, 'a');
	RIWO_TEST_CHECK_EQ(sha256(block).hex(false),
		"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
	sha256 streamed;
	streamed.append(std::string_view(block).substr(0, 63)).append("aa");
	RIWO_TEST_CHECK_EQ(streamed.hex(false),
		"635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
	RIWO_TEST_CHECK_EQ(sha256(std::string(1'000'000, 'a')).hex(false),
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void hmac_sha256_vectors()
{
	using namespace riwo::crypto;
	// RFC 4231, cases 1, 2, and 6 (a key longer than one SHA-256 block).
	const std::array<std::byte,20> key = [] {
		std::array<std::byte,20> bytes;
		bytes.fill(std::byte {0x0B});
		return bytes;
	}();
	const auto message = std::as_bytes(std::span(std::string_view("Hi There")));
	const auto tag = hmac_sha256(key, message);
	RIWO_TEST_CHECK_EQ(base64_encode(tag), "sDRMYdjbOFNcqK/OrwvxK4gdwgDJgz2nJuk3bC4yz/c=");
	RIWO_TEST_CHECK(hmac_sha256_verify(key, message, tag));

	auto altered = tag;
	altered.back() ^= std::byte {1};
	RIWO_TEST_CHECK(not hmac_sha256_verify(key, message, altered));
	RIWO_TEST_CHECK(not hmac_sha256_verify(key, message, std::span(tag).first<31>()));
	RIWO_TEST_CHECK(not hmac_sha256_verify(key, message.first<7>(), tag));

	const auto text_tag = hmac_sha256("Jefe", "what do ya want for nothing?");
	RIWO_TEST_CHECK_EQ(base64_encode(text_tag), "W9zBRr9gdU5qBCQmCJV1x1oAPwidJzmDnexYuWTsOEM=");
	RIWO_TEST_CHECK(hmac_sha256_verify("Jefe", "what do ya want for nothing?", text_tag));
	RIWO_TEST_CHECK_EQ(base64_encode(hmac_sha256(std::string(131, static_cast<char>(0xAA)),
		"Test Using Larger Than Block-Size Key - Hash Key First")),
		"YOQxWR7gtn8Niiaqy/W3f44LxiE3KMUUBUYEDw7jf1Q=");
	RIWO_TEST_CHECK_EQ(base64_encode(hmac_sha256("", "")),
		"thNnmggU2ex3L5XXeMNfxf8Wl8STcVZTxscSFEKSxa0=");
}

void base64_vectors()
{
	using namespace riwo::crypto;
	constexpr std::pair<std::string_view,std::string_view> vectors[] {
		{"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
		{"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}
	};
	for(const auto &[raw, encoded] : vectors)
	{
		RIWO_TEST_CHECK_EQ(base64_encode(raw), encoded);
		RIWO_TEST_CHECK_EQ(base64_decode(encoded).value(), raw);
	}
	const std::array binary {std::byte {0xFF}, std::byte {0xEE}};
	const std::string_view binary_text("\xFF\xEE", 2);
	RIWO_TEST_CHECK_EQ(base64_encode(binary), "/+4=");
	RIWO_TEST_CHECK_EQ(base64_decode("/+4=").value(), binary_text);
	RIWO_TEST_CHECK_EQ(base64_decode("AA==").value(), std::string(1, '\0'));
	RIWO_TEST_CHECK_EQ(base64url_encode(binary), "_-4");
	RIWO_TEST_CHECK_EQ(base64url_encode(binary_text, true), "_-4=");
	RIWO_TEST_CHECK_EQ(base64url_decode("_-4").value(), binary_text);
	RIWO_TEST_CHECK_EQ(base64url_decode("_-4=").value(), binary_text);
	RIWO_TEST_CHECK_EQ(base64url_decode("Zg").value(), "f");
	RIWO_TEST_CHECK_EQ(base64url_decode("").value(), "");

	for(const auto invalid : {"Z", "Zg=", "Zg===", "Z=g=", "Zg==Zg==", "Z g=", "====", "Zg==\n"})
	{
		RIWO_TEST_CHECK(not base64_decode(invalid));
		RIWO_TEST_CHECK(not base64url_decode(invalid));
	}
	RIWO_TEST_CHECK(not base64_decode("Zg"));
	RIWO_TEST_CHECK(not base64_decode("_-4="));
	RIWO_TEST_CHECK(not base64url_decode("/+4="));
}

void secure_random()
{
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
		{"SHA-256 vectors", sha256_vectors},
		{"HMAC-SHA256 vectors", hmac_sha256_vectors},
		{"Base64 and Base64URL vectors", base64_vectors},
		{"Secure random", secure_random},
		{"UUID values", uuid_values},
	});
}
