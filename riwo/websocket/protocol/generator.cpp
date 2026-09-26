// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "generator.h"
#include "detail/utf8.h"

namespace riwo::websocket
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

	std::array<std::byte,8> expanded {};
	for(size_t index=0; index<expanded.size(); ++index)
		expanded[index] = key.bytes[(initial + index) & 0x03];

	uint64_t mask = 0;
	std::memcpy(&mask, expanded.data(), sizeof(mask));

	size_t index = 0;
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

	if( source.size() != 0 )
		std::memmove(destination.data(), source.data(), source.size());

	apply_mask(mutable_buffer(destination.data(), source.size()), key, payload_offset);
	return source.size();
}

} //namespace riwo::websocket
