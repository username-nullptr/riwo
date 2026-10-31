// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "generator.h"
#include "detail/utf8.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
# include <emmintrin.h>
#elif defined(__SSE2__)
# include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
# include <arm_neon.h>
#endif

namespace riwo::experimental::websocket
{

const_buffer encoded_frame_header::buffer() const noexcept
{
	return { storage.data(), size };
}

const_buffer encoded_close_payload::buffer() const noexcept
{
	return { storage.data(), size };
}

sys_expected<encoded_frame_header> encode_frame_header
(const frame_header &header, frame_codec_config config) noexcept
{
	if( not is_known_opcode(header.op) )
		return sys_unexpected(make_error_code(protocol_errc::reserved_opcode));

	const auto rsv = header.rsv.value<uint8_t>();
	const auto allowed_rsv = config.allowed_rsv.value<uint8_t>();

	if( (rsv & 0xF8) != 0 or (rsv & ~allowed_rsv) != 0 )
		return sys_unexpected(make_error_code(protocol_errc::unexpected_rsv));

	if( header.payload_size > 0x7FFFFFFFFFFFFFFFULL )
		return sys_unexpected(make_error_code(protocol_errc::invalid_64bit_length));

	if( is_control_opcode(header.op) )
	{
		if( not header.fin )
			return sys_unexpected(make_error_code(protocol_errc::fragmented_control_frame));

		if( header.payload_size > 125 )
			return sys_unexpected(make_error_code(protocol_errc::ctrl_payload_too_large));
	}
	if( config.max_frame_size != 0 and header.payload_size > config.max_frame_size )
		return sys_unexpected(make_error_code(protocol_errc::frame_too_large));

	if( config.local_role == role::client and not header.mask )
		return sys_unexpected(make_error_code(protocol_errc::missing_mask));

	if( config.local_role == role::server and header.mask )
		return sys_unexpected(make_error_code(protocol_errc::unexpected_mask));

	encoded_frame_header result;
	auto &storage = result.storage;

	storage[result.size++] = static_cast<std::byte> (
		(header.fin ? 0x80 : 0x00) | (rsv << 4) | static_cast<uint8_t>(header.op)
	);
	const auto mask_flag = header.mask ? uint8_t {0x80} : uint8_t {0};
	if( header.payload_size <= 125 )
		storage[result.size++] = static_cast<std::byte>(mask_flag | header.payload_size);

	else if( header.payload_size <= 0xFFFF )
	{
		storage[result.size++] = static_cast<std::byte>(mask_flag | 126);
		storage[result.size++] = static_cast<std::byte>(header.payload_size >> 8);
		storage[result.size++] = static_cast<std::byte>(header.payload_size);
	}
	else
	{
		storage[result.size++] = static_cast<std::byte>(mask_flag | 127);
		for(int shift=56; shift>=0; shift-=8)
			storage[result.size++] = static_cast<std::byte>(header.payload_size >> shift);
	}
	if( header.mask )
	{
		for(const auto value : header.mask->bytes)
			storage[result.size++] = value;
	}
	return result;
}

sys_expected<encoded_close_payload> encode_close_payload(const close_frame &frame) noexcept
{
	return encode_close_payload(close_payload_view { frame.code, frame.reason });
}

sys_expected<encoded_close_payload> encode_close_payload(close_payload_view payload) noexcept
{
	if( not payload.code )
	{
		if( not payload.reason.empty() )
			return sys_unexpected(make_error_code(protocol_errc::invalid_close_payload));
		return encoded_close_payload {};
	}
	if( not is_valid_close_code(*payload.code) )
		return sys_unexpected(make_error_code(protocol_errc::invalid_close_payload));

	if( payload.reason.size() > 123 )
		return sys_unexpected(make_error_code(protocol_errc::ctrl_payload_too_large));

	if( not detail::is_valid_utf8(payload.reason) )
		return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));

	encoded_close_payload result;
	result.storage[0] = static_cast<std::byte>(*payload.code >> 8);
	result.storage[1] = static_cast<std::byte>(*payload.code);

	if( not payload.reason.empty() )
	{
		std::memcpy(result.storage.data() + 2,
			payload.reason.data(), payload.reason.size());
	}
	result.size = static_cast<uint8_t>(payload.reason.size() + 2);
	return result;
}

void apply_mask(const mutable_buffer &payload, const masking_key &key, uint64_t payload_offset) noexcept
{
	auto *data = static_cast<std::byte*>(payload.data());
	const auto initial = payload_offset & 0x03;

	std::array<std::byte,16> expanded {};
	for(size_t index=0; index<expanded.size(); ++index)
		expanded[index] = key.bytes[(initial + index) & 0x03];

	size_t index = 0;
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))) || defined(__SSE2__)
	const auto vector_mask = _mm_loadu_si128 (
		reinterpret_cast<const __m128i*>(expanded.data())
	);
	for(; payload.size()-index>=sizeof(__m128i); index+=sizeof(__m128i))
	{
		auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + index));
		value = _mm_xor_si128(value, vector_mask);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(data + index), value);
	}
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
	const auto vector_mask = vld1q_u8(reinterpret_cast<const uint8_t*>(expanded.data()));
	for(; payload.size()-index>=sizeof(uint8x16_t); index+=sizeof(uint8x16_t))
	{
		auto value = vld1q_u8(reinterpret_cast<const uint8_t*>(data + index));
		value = veorq_u8(value, vector_mask);
		vst1q_u8(reinterpret_cast<uint8_t*>(data + index), value);
	}
#endif //CPU

	uint64_t mask = 0;
	std::memcpy(&mask, expanded.data(), sizeof(mask));

	for(; payload.size()-index>=sizeof(uint64_t); index+=sizeof(uint64_t))
	{
		uint64_t value = 0;
		std::memcpy(&value, data + index, sizeof(value));

		value ^= mask;
		std::memcpy(data + index, &value, sizeof(value));
	}
	for(; index<payload.size(); ++index)
		data[index] ^= key.bytes[(initial + index) & 0x03];
}

sys_expected<size_t> mask_copy(const mutable_buffer &destination, const const_buffer &source,
	const masking_key &key, uint64_t payload_offset) noexcept
{
	if( destination.size() < source.size() )
		return sys_unexpected(make_system_error_code(std::errc::no_buffer_space));

	if( source.size() == 0 )
		return size_t {0};

	auto *target = static_cast<std::byte*>(destination.data());
	const auto *input = static_cast<const std::byte*>(source.data());

	if( target == input )
	{
		apply_mask(mutable_buffer(target, source.size()), key, payload_offset);
		return source.size();
	}
	const auto target_address = reinterpret_cast<uintptr_t>(target);
	const auto input_address = reinterpret_cast<uintptr_t>(input);

	const bool overlap = target_address < input_address ?
		input_address - target_address < source.size() :
		target_address - input_address < source.size();

	if( overlap )
	{
		std::memmove(target, input, source.size());
		apply_mask(mutable_buffer(target, source.size()), key, payload_offset);
		return source.size();
	}
	const auto initial = payload_offset & 0x03;
	std::array<std::byte,16> expanded {};

	for(size_t index=0; index<expanded.size(); ++index)
		expanded[index] = key.bytes[(initial + index) & 0x03];

	size_t index = 0;
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))) || defined(__SSE2__)
	const auto vector_mask = _mm_loadu_si128(
		reinterpret_cast<const __m128i*>(expanded.data())
	);
	for(; source.size()-index>=sizeof(__m128i); index+=sizeof(__m128i))
	{
		auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + index));
		value = _mm_xor_si128(value, vector_mask);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(target + index), value);
	}
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
	const auto vector_mask = vld1q_u8(reinterpret_cast<const uint8_t*>(expanded.data()));
	for(; source.size()-index>=sizeof(uint8x16_t); index+=sizeof(uint8x16_t))
	{
		auto value = vld1q_u8(reinterpret_cast<const uint8_t*>(input + index));
		value = veorq_u8(value, vector_mask);
		vst1q_u8(reinterpret_cast<uint8_t*>(target + index), value);
	}
#endif //CPU
	uint64_t scalar_mask = 0;
	std::memcpy(&scalar_mask, expanded.data(), sizeof(scalar_mask));

	for(; source.size()-index>=sizeof(uint64_t); index+=sizeof(uint64_t))
	{
		uint64_t value = 0;
		std::memcpy(&value, input + index, sizeof(value));

		value ^= scalar_mask;
		std::memcpy(target + index, &value, sizeof(value));
	}
	for(; index<source.size(); ++index)
		target[index] = input[index] ^ key.bytes[(initial + index) & 0x03];

	return source.size();
}

} //namespace riwo::experimental::websocket
