// SPDX-FileCopyrightText: 2024 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "sha1.h"

namespace riwo
{

class RIWO_DECL_HIDDEN sha1::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	impl() = default;

	void add_byte_dont_count_bits(uint8_t x)
	{
		m_buf[m_i++] = x;
		if( m_i >= sizeof(m_buf) )
		{
			m_i = 0;
			process_block(m_buf);
		}
	}

	void process_block(const uint8_t *ptr)
	{
		static constexpr uint32_t c0 = 0x5A827999;
		static constexpr uint32_t c1 = 0x6ED9EBA1;
		static constexpr uint32_t c2 = 0x8F1BBCDC;
		static constexpr uint32_t c3 = 0xCA62C1D6;

		uint32_t a = m_state[0];
		uint32_t b = m_state[1];
		uint32_t c = m_state[2];
		uint32_t d = m_state[3];
		uint32_t e = m_state[4];

		uint32_t w[16];
		for(int i=0; i<16; i++)
			w[i] = make_word(ptr + (i << 2));

#define SHA1_LOAD(i) \
	w[(i) & 15] = rol32(w[((i) + 13) & 15] ^ w[((i) + 8) & 15] ^ w[((i) + 2) & 15] ^ w[(i) & 15], 1);

#define SHA1_ROUND_0(v,u,x,y,z,i) \
	z += (((u) & ((x) ^ (y))) ^ (y)) + w[(i) & 15] + c0 + rol32((v), 5); (u) = rol32((u), 30);

#define SHA1_ROUND_1(v,u,x,y,z,i) \
	SHA1_LOAD(i) (z) += (((u) & ((x) ^ (y))) ^ (y)) + w[(i) & 15] + c0 + rol32((v), 5); (u) = rol32((u), 30);

#define SHA1_ROUND_2(v,u,x,y,z,i) \
	SHA1_LOAD(i) (z) += ((u) ^ (x) ^ (y)) + w[(i) & 15] + c1 + rol32((v), 5); (u) = rol32((u), 30);

#define SHA1_ROUND_3(v,u,x,y,z,i) \
	SHA1_LOAD(i) (z) += ((((u) | (x)) & (y)) | ((u) & (x))) + w[(i) & 15] + c2 + rol32((v), 5); (u) = rol32((u), 30);

#define SHA1_ROUND_4(v,u,x,y,z,i) \
	SHA1_LOAD(i) (z) += ((u) ^ (x) ^ (y)) + w[(i) & 15] + c3 + rol32((v), 5); (u) = rol32((u), 30);

		SHA1_ROUND_0(a, b, c, d, e,  0);
		SHA1_ROUND_0(e, a, b, c, d,  1);
		SHA1_ROUND_0(d, e, a, b, c,  2);
		SHA1_ROUND_0(c, d, e, a, b,  3);
		SHA1_ROUND_0(b, c, d, e, a,  4);
		SHA1_ROUND_0(a, b, c, d, e,  5);
		SHA1_ROUND_0(e, a, b, c, d,  6);
		SHA1_ROUND_0(d, e, a, b, c,  7);
		SHA1_ROUND_0(c, d, e, a, b,  8);
		SHA1_ROUND_0(b, c, d, e, a,  9);
		SHA1_ROUND_0(a, b, c, d, e, 10);
		SHA1_ROUND_0(e, a, b, c, d, 11);
		SHA1_ROUND_0(d, e, a, b, c, 12);
		SHA1_ROUND_0(c, d, e, a, b, 13);
		SHA1_ROUND_0(b, c, d, e, a, 14);
		SHA1_ROUND_0(a, b, c, d, e, 15);
		SHA1_ROUND_1(e, a, b, c, d, 16);
		SHA1_ROUND_1(d, e, a, b, c, 17);
		SHA1_ROUND_1(c, d, e, a, b, 18);
		SHA1_ROUND_1(b, c, d, e, a, 19);
		SHA1_ROUND_2(a, b, c, d, e, 20);
		SHA1_ROUND_2(e, a, b, c, d, 21);
		SHA1_ROUND_2(d, e, a, b, c, 22);
		SHA1_ROUND_2(c, d, e, a, b, 23);
		SHA1_ROUND_2(b, c, d, e, a, 24);
		SHA1_ROUND_2(a, b, c, d, e, 25);
		SHA1_ROUND_2(e, a, b, c, d, 26);
		SHA1_ROUND_2(d, e, a, b, c, 27);
		SHA1_ROUND_2(c, d, e, a, b, 28);
		SHA1_ROUND_2(b, c, d, e, a, 29);
		SHA1_ROUND_2(a, b, c, d, e, 30);
		SHA1_ROUND_2(e, a, b, c, d, 31);
		SHA1_ROUND_2(d, e, a, b, c, 32);
		SHA1_ROUND_2(c, d, e, a, b, 33);
		SHA1_ROUND_2(b, c, d, e, a, 34);
		SHA1_ROUND_2(a, b, c, d, e, 35);
		SHA1_ROUND_2(e, a, b, c, d, 36);
		SHA1_ROUND_2(d, e, a, b, c, 37);
		SHA1_ROUND_2(c, d, e, a, b, 38);
		SHA1_ROUND_2(b, c, d, e, a, 39);
		SHA1_ROUND_3(a, b, c, d, e, 40);
		SHA1_ROUND_3(e, a, b, c, d, 41);
		SHA1_ROUND_3(d, e, a, b, c, 42);
		SHA1_ROUND_3(c, d, e, a, b, 43);
		SHA1_ROUND_3(b, c, d, e, a, 44);
		SHA1_ROUND_3(a, b, c, d, e, 45);
		SHA1_ROUND_3(e, a, b, c, d, 46);
		SHA1_ROUND_3(d, e, a, b, c, 47);
		SHA1_ROUND_3(c, d, e, a, b, 48);
		SHA1_ROUND_3(b, c, d, e, a, 49);
		SHA1_ROUND_3(a, b, c, d, e, 50);
		SHA1_ROUND_3(e, a, b, c, d, 51);
		SHA1_ROUND_3(d, e, a, b, c, 52);
		SHA1_ROUND_3(c, d, e, a, b, 53);
		SHA1_ROUND_3(b, c, d, e, a, 54);
		SHA1_ROUND_3(a, b, c, d, e, 55);
		SHA1_ROUND_3(e, a, b, c, d, 56);
		SHA1_ROUND_3(d, e, a, b, c, 57);
		SHA1_ROUND_3(c, d, e, a, b, 58);
		SHA1_ROUND_3(b, c, d, e, a, 59);
		SHA1_ROUND_4(a, b, c, d, e, 60);
		SHA1_ROUND_4(e, a, b, c, d, 61);
		SHA1_ROUND_4(d, e, a, b, c, 62);
		SHA1_ROUND_4(c, d, e, a, b, 63);
		SHA1_ROUND_4(b, c, d, e, a, 64);
		SHA1_ROUND_4(a, b, c, d, e, 65);
		SHA1_ROUND_4(e, a, b, c, d, 66);
		SHA1_ROUND_4(d, e, a, b, c, 67);
		SHA1_ROUND_4(c, d, e, a, b, 68);
		SHA1_ROUND_4(b, c, d, e, a, 69);
		SHA1_ROUND_4(a, b, c, d, e, 70);
		SHA1_ROUND_4(e, a, b, c, d, 71);
		SHA1_ROUND_4(d, e, a, b, c, 72);
		SHA1_ROUND_4(c, d, e, a, b, 73);
		SHA1_ROUND_4(b, c, d, e, a, 74);
		SHA1_ROUND_4(a, b, c, d, e, 75);
		SHA1_ROUND_4(e, a, b, c, d, 76);
		SHA1_ROUND_4(d, e, a, b, c, 77);
		SHA1_ROUND_4(c, d, e, a, b, 78);
		SHA1_ROUND_4(b, c, d, e, a, 79);

#undef SHA1_LOAD
#undef SHA1_ROUND_0
#undef SHA1_ROUND_1
#undef SHA1_ROUND_2
#undef SHA1_ROUND_3
#undef SHA1_ROUND_4

		m_state[0] += a;
		m_state[1] += b;
		m_state[2] += c;
		m_state[3] += d;
		m_state[4] += e;
	}

private:
	static uint32_t make_word(const uint8_t *p)
	{
		return ( static_cast<uint32_t>(p[0]) << 24 ) |
			   ( static_cast<uint32_t>(p[1]) << 16 ) |
			   ( static_cast<uint32_t>(p[2]) <<  8 ) |
			   ( static_cast<uint32_t>(p[3]) <<  0 );
	}

	static uint32_t rol32(uint32_t x, uint32_t n)
	{
		return (x << n) | (x >> (32 - n));
	}

public:
	uint32_t m_state[5]
	{
		0x67452301,
		0xEFCDAB89,
		0x98BADCFE,
		0x10325476,
		0xC3D2E1F0
	};
	uint8_t m_buf[64] {0};
	uint32_t m_i = 0;
	uint64_t m_n_bits = 0;
};

/*----------------------------------------------------------------------------------------------------------*/

sha1::sha1() :
	m_impl(new impl())
{

}

sha1::sha1(std::string_view text) :
	m_impl(new impl())
{
	append(text);
}

sha1::sha1(const sha1 &other) :
	m_impl(new impl())
{
	operator=(other);
}

sha1::sha1(sha1 &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl();
}

sha1::~sha1()
{
	delete m_impl;
}

sha1 &sha1::operator=(const sha1 &other)
{
	if( this == &other )
		return *this;

	std::memcpy(m_impl->m_state, other.m_impl->m_state, sizeof(m_impl->m_state));
	std::memcpy(m_impl->m_buf, other.m_impl->m_buf, sizeof(m_impl->m_buf));

	m_impl->m_i = other.m_impl->m_i;
	m_impl->m_n_bits = other.m_impl->m_n_bits;
	return *this;
}

sha1 &sha1::operator=(sha1 &&other) noexcept
{
	if( this == &other )
		return *this;
	delete m_impl;
	m_impl = other.m_impl;
	other.m_impl = new impl();
	return *this;
}

sha1 &sha1::append(uint8_t x)
{
	m_impl->add_byte_dont_count_bits(x);
	m_impl->m_n_bits += 8;
	return *this;
}

sha1 &sha1::append(char c)
{
	return append(static_cast<uint8_t>(c));
}

sha1 &sha1::append(const void *data, size_t size)
{
	if( data == nullptr )
	{
		invalid_argument::loc_throw (
			"riwo::sha1::append: data is nullptr"
		);
	}
	auto ptr = static_cast<const uint8_t*>(data);
	for(; size and m_impl->m_i % sizeof(m_impl->m_buf); size--)
		append(*ptr++);

	for(; size >= sizeof(m_impl->m_buf); size -= sizeof(m_impl->m_buf))
	{
		m_impl->process_block(ptr);
		ptr += sizeof(m_impl->m_buf);
		m_impl->m_n_bits += sizeof(m_impl->m_buf) << 3;
	}
	for(; size; size--)
		append(*ptr++);
	return *this;
}

sha1 &sha1::append(std::string_view text)
{
	if( text.empty() )
		return *this;
	return append(text.data(), text.size());
}

void sha1::operator+=(uint8_t x)
{
	append(x);
}

void sha1::operator+=(char c)
{
	append(c);
}

void sha1::operator+=(const std::string &text)
{
	append(text);
}

sha1& sha1::finalize()
{
	m_impl->add_byte_dont_count_bits(0x80);
	while( m_impl->m_i % 64 != 56 )
		m_impl->add_byte_dont_count_bits(0x00);
	for(int j=7; j>=0; j--)
		m_impl->add_byte_dont_count_bits(static_cast<uint8_t>(m_impl->m_n_bits >> (j << 3)));
	return *this;
}

std::string sha1::hex(bool upper_case) const
{
	static constexpr const char *lower = "0123456789abcdef";
	static constexpr const char *upper = "0123456789ABCDEF";

	auto alphabet = upper_case ? upper : lower;
	char _hex[64] = "";
	int k = 0;

//	for(int i=0; i<5; i++)
	for(int i : {0,1,2,3,4})
	{
		for(int j=7; j>=0; j--)
			_hex[k++] = alphabet[(m_impl->m_state[i] >> (j << 2)) & 0x0F];
	}
	return _hex;
}

std::string sha1::base64() const
{
	static constexpr const char *table =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";

	uint32_t triples[7]
	{
		( (m_impl->m_state[0] & 0xFFFFFF00) >> (1 << 3) ),
		( (m_impl->m_state[0] & 0x000000FF) << (2 << 3) ) | ( (m_impl->m_state[1] & 0xFFFF0000) >> (2 << 3) ),
		( (m_impl->m_state[1] & 0x0000FFFF) << (1 << 3) ) | ( (m_impl->m_state[2] & 0xFF000000) >> (3 << 3) ),
		( (m_impl->m_state[2] & 0x00FFFFFF) << (0 << 3) ),
		( (m_impl->m_state[3] & 0xFFFFFF00) >> (1 << 3) ),
		( (m_impl->m_state[3] & 0x000000FF) << (2 << 3) ) | ( (m_impl->m_state[4] & 0xFFFF0000) >> (2 << 3) ),
		( (m_impl->m_state[4] & 0x0000FFFF) << (1 << 3) ),
	};
	char _base64[32] = "";
	int i = 0;

	for(; i<7; i++)
	{
		auto x = triples[i];
		_base64[(i << 2) + 0] = table[(x >> 3 * 6) % 64];
		_base64[(i << 2) + 1] = table[(x >> 2 * 6) % 64];
		_base64[(i << 2) + 2] = table[(x >> 1 * 6) % 64];
		_base64[(i << 2) + 3] = table[(x >> 0 * 6) % 64];
	}
	_base64[(--i << 2) + 3] = '=';
	return _base64;
}

} //namespace riwo
