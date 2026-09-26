// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_PROTOCOL_GENERATOR_H
#define RIWO_WEBSOCKET_PROTOCOL_GENERATOR_H

#include <riwo/websocket/protocol/types.h>

namespace riwo::websocket
{

struct RIWO_WEBSOCKET_API encoded_frame_header
{
	std::array<std::byte,14> storage {};
	uint8_t size = 0;

	[[nodiscard]] const_buffer buffer() const noexcept;
};

struct RIWO_WEBSOCKET_API encoded_close_payload
{
	std::array<std::byte,125> storage {};
	uint8_t size = 0;

	[[nodiscard]] const_buffer buffer() const noexcept;
};

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<encoded_frame_header> encode_frame_header (
	const frame_header &header, frame_codec_config config
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<encoded_close_payload> encode_close_payload(const close_frame &frame) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<encoded_close_payload> encode_close_payload(close_payload_view payload) noexcept;

RIWO_WEBSOCKET_API void apply_mask (
	const mutable_buffer &payload, const masking_key &key, uint64_t payload_offset = 0
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<size_t> mask_copy (
	const mutable_buffer &destination, const const_buffer &source,
	const masking_key &key, uint64_t payload_offset = 0
) noexcept;

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_PROTOCOL_GENERATOR_H
