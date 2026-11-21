// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "hmac_sha256.h"

namespace riwo::crypto
{

sha256::digest_t hmac_sha256(span_t<> key, span_t<> message)
{
	constexpr std::byte inner_pad {0x36};
	constexpr std::byte outer_pad {0x5C};
	std::array<std::byte,sha256::block_size> key_block {};

	if( key.size() > key_block.size() )
		std::ranges::copy(sha256(key).digest(), key_block.begin());
	else
		std::ranges::copy(key, key_block.begin());

	for(auto &byte : key_block)
		byte ^= inner_pad;

	const auto inner_digest = sha256(key_block).append(message).digest();

	for(auto &byte : key_block)
		byte ^= inner_pad ^ outer_pad;

	return sha256(key_block).append(inner_digest).digest();
}

sha256::digest_t hmac_sha256(std::string_view key, std::string_view message)
{
	return hmac_sha256(std::as_bytes(std::span(key)), std::as_bytes(std::span(message)));
}

bool hmac_sha256_verify(span_t<> key, span_t<> message, span_t<> tag)
{
	if( tag.size() != sha256::digest_size )
		return false;

	const auto actual = hmac_sha256(key, message);

	// Observable updates prevent the compiler from replacing the loop with an early-exit comparison.
	volatile unsigned int difference = 0;

	for(size_t i=0; i<actual.size(); i++)
		difference = difference | std::to_integer<unsigned int>(actual[i] ^ tag[i]);
	return difference == 0;
}

bool hmac_sha256_verify(std::string_view key, std::string_view message, span_t<> tag)
{
	return hmac_sha256_verify(std::as_bytes(std::span(key)), std::as_bytes(std::span(message)), tag);
}

} //namespace riwo::crypto
