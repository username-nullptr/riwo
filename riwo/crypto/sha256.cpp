// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "sha256.h"
#include "base64.h"

namespace riwo::crypto { namespace
{

// SHA-256 round constants from RFC 6234, Section 5.1.
constexpr std::array<uint32_t,64> g_round_constants {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

} //namespace

class RIWO_DECL_HIDDEN sha256::impl
{
public:
	void process_block(span_t<block_size> block) noexcept
	{
		std::array<uint32_t,64> words {};
		for(size_t i=0; i<16; i++)
		{
			const auto offset = i * 4;
			words[i] =
				(std::to_integer<uint32_t>(block[offset    ]) << 24) |
				(std::to_integer<uint32_t>(block[offset + 1]) << 16) |
				(std::to_integer<uint32_t>(block[offset + 2]) << 8 ) |
				(std::to_integer<uint32_t>(block[offset + 3]) << 0 );
		}
		for(size_t i=16; i<words.size(); i++)
		{
			const auto x = words[i - 15];
			const auto y = words[i - 2];

			const auto sigma0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
			const auto sigma1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);

			words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
		}
		auto [a, b, c, d, e, f, g, h] = m_state;

		for(size_t i=0; i<words.size(); i++)
		{
			const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
			const auto choose = (e & f) ^ (~e & g);

			const auto temp1 = h + sum1 + choose + g_round_constants[i] + words[i];
			const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);

			const auto majority = (a & b) ^ (a & c) ^ (b & c);
			const auto temp2 = sum0 + majority;

			h = g;
			g = f;
			f = e;
			e = d + temp1;

			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}
		const std::array working_state {a, b, c, d, e, f, g, h};

		for(size_t i=0; i<m_state.size(); i++)
			m_state[i] += working_state[i];
	}

public:
	std::array<uint32_t,8> m_state {
		0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
		0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
	};
	std::array<std::byte,block_size> m_buffer {};

	uint64_t m_size = 0;
	bool m_finalized = false;
};

sha256::sha256(span_t<> input) :
	m_impl(new impl())
{
	append(input);
}

sha256::sha256(std::string_view input) :
	sha256(std::as_bytes(std::span(input)))
{

}

sha256::sha256() :
	m_impl(new impl())
{

}

sha256::~sha256()
{
	delete m_impl;
}

sha256::sha256(const sha256 &other) :
	m_impl(new impl(*other.m_impl))
{
	operator=(other);
}

sha256::sha256(sha256 &&other) noexcept :
	m_impl(new impl(*other.m_impl))
{

}

sha256 &sha256::operator=(const sha256 &other)
{
	if( this != &other )
		*m_impl = *other.m_impl;
	return *this;
}

sha256 &sha256::operator=(sha256 &&other) noexcept
{
	if( this != &other )
		*m_impl = *other.m_impl;
	return *this;
}

sha256 &sha256::append(span_t<> input)
{
	if( m_impl->m_finalized )
		logic_error::loc_throw("Cannot append to a finalized SHA-256 hash");

	constexpr auto max_bytes = std::numeric_limits<uint64_t>::max() / 8;
	if( input.size() > max_bytes - m_impl->m_size )
		invalid_argument::loc_throw("SHA-256 input exceeds the maximum message length");

	if( input.empty() )
		return *this;

	const auto buffered = m_impl->m_size % block_size;
	m_impl->m_size += input.size();

	if( buffered != 0 )
	{
		auto tail = std::span(m_impl->m_buffer).subspan(buffered);
		const auto copied = std::min(tail.size(), input.size());

		std::ranges::copy(input.first(copied), tail.begin());
		if( copied < tail.size() )
			return *this;

		m_impl->process_block(m_impl->m_buffer);
		input = input.subspan(copied);
	}
	while( input.size() >= block_size )
	{
		m_impl->process_block(input.first<block_size>());
		input = input.subspan(block_size);
	}
	std::ranges::copy(input, m_impl->m_buffer.begin());
	return *this;
}

sha256 &sha256::append(std::string_view input)
{
	return append(std::as_bytes(std::span(input)));
}

sha256 &sha256::finalize() noexcept
{
	if( m_impl->m_finalized )
		return *this;

	constexpr size_t length_offset = block_size - sizeof(uint64_t);
	auto buffered = m_impl->m_size % block_size;

	m_impl->m_buffer[buffered++] = std::byte {0x80};
	if( buffered > length_offset )
	{
		std::ranges::fill(std::span(m_impl->m_buffer).subspan(buffered), std::byte {});
		m_impl->process_block(m_impl->m_buffer);
		buffered = 0;
	}
	std::ranges::fill(std::span(m_impl->m_buffer)
		.subspan(buffered, length_offset - buffered), std::byte {});

	const auto bit_count = m_impl->m_size * 8;
	for(size_t byte = 0; byte < sizeof(bit_count); ++byte)
		m_impl->m_buffer[block_size - 1 - byte] = static_cast<std::byte>((bit_count >> (byte * 8)) & 0xFF);

	m_impl->process_block(m_impl->m_buffer);
	m_impl->m_finalized = true;
	return *this;
}

sha256::digest_t sha256::digest() const noexcept
{
	auto snapshot = *this;
	snapshot.finalize();

	digest_t result;
	for(size_t word = 0; word < snapshot.m_impl->m_state.size(); ++word)
	{
		const auto value = snapshot.m_impl->m_state[word];
		for(size_t byte = 0; byte < 4; ++byte)
			result[word * 4 + byte] = static_cast<std::byte>((value >> ((3 - byte) * 8)) & 0xFF);
	}
	return result;
}

std::string sha256::hex(bool upper_case) const
{
	const auto alphabet = upper_case ?
		"0123456789ABCDEF" : "0123456789abcdef";

	std::string result {};
	result.reserve(digest_size * 2);

	for(const auto byte : digest())
	{
		const auto value = std::to_integer<uint8_t>(byte);
		result += alphabet[value >> 4];
		result += alphabet[value & 0xF];
	}
	return result;
}

std::string sha256::base64() const
{
	return base64_encode(digest());
}

} //namespace riwo::crypto
