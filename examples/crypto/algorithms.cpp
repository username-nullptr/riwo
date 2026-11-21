// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/crypto.h>
#include <iostream>

int main()
{
	using namespace riwo::crypto;

	constexpr std::string_view message = "Hello from Riwo";
	constexpr std::string_view demo_key = "example-only shared key";

	sha1 legacy_digest;
	legacy_digest.append(std::as_bytes(std::span(message.data(), message.size())));
	std::cout << "SHA-1: " << legacy_digest.finalize().hex() << '\n';

	sha256 digest;
	digest.append("Hello from ").append("Riwo");
	std::cout << "SHA-256: " << digest.finalize().hex() << '\n';

	const auto encoded = base64_encode(message);
	const auto decoded = base64_decode(encoded);

	if( not decoded )
	{
		std::cerr << "Invalid Base64 message\n";
		return 1;
	}
	std::cout << "Base64: " << encoded << " -> " << *decoded << '\n';

	const auto tag = hmac_sha256(demo_key, message);
	const auto encoded_tag = base64url_encode(tag);
	const auto decoded_tag = base64url_decode(encoded_tag);

	if( not decoded_tag )
	{
		std::cerr << "Invalid Base64URL tag\n";
		return 1;
	}
	// Verification consumes the decoded bytes of the full tag.
	const auto tag_bytes = std::as_bytes (
		std::span(decoded_tag->data(), decoded_tag->size())
	);
	if( not hmac_sha256_verify(demo_key, message, tag_bytes) )
	{
		std::cerr << "HMAC-SHA256 verification failed\n";
		return 1;
	}
	std::cout << "Verified HMAC-SHA256 tag (Base64URL): " << encoded_tag << '\n';
	std::cout << "UUID: " << uuid::generate().to_string() << '\n';
	return 0;
}
