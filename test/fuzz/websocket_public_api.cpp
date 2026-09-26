// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/ctrl_payload.h>
#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/parser.h>
#include <riwo/websocket/protocol/types.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	const std::string input(reinterpret_cast<const char*>(data), size);
	std::vector<std::byte> bytes(size);
	std::memcpy(bytes.data(), data, size);
	riwo::websocket::ctrl_payload control(bytes);
	riwo::ignore_unused(control.text());
	riwo::ignore_unused(control.as_mutable_buffer());
	riwo::ignore_unused(control.as_const_buffer());
	control.resize(data[0] % 126);
	const auto assigned = std::string_view(input).substr(0, data[0] % size);
	control.assign(assigned);
	if(control.text() != assigned or control.size() != assigned.size())
		std::abort();
	if((data[0] & 1U) != 0)
		control.clear();

	riwo::websocket::masking_key key;
	for(size_t index = 0; index < key.bytes.size(); ++index)
		key.bytes[index] = std::byte {data[index % size]};
	riwo::websocket::frame_header header;
	header.fin = (data[0] & 1U) != 0;
	header.rsv = riwo::websocket::reserved_bits(
		static_cast<riwo::websocket::reserved_bit>(data[0] & 0x07U));
	header.op = static_cast<riwo::websocket::opcode>(data[0] & 0x0FU);
	header.payload_size = size > 1 ? data[1] : size;
	if((data[0] & 2U) != 0)
		header.mask = key;
	riwo::websocket::frame_codec_config config;
	config.local_role = (data[0] & 4U) != 0 ?
		riwo::websocket::role::server : riwo::websocket::role::client;
	config.max_frame_size = size > 2 ? data[2] : size;
	config.allowed_rsv = riwo::websocket::reserved_bits(
		static_cast<riwo::websocket::reserved_bit>((data[0] >> 3U) & 0x07U));
	riwo::ignore_unused(riwo::websocket::encode_frame_header(header, config));

	const uint16_t close_code = size > 1 ?
		static_cast<uint16_t>((uint16_t(data[0]) << 8U) | data[1]) : data[0];
	riwo::ignore_unused(riwo::websocket::is_known_opcode(header.op));
	riwo::ignore_unused(riwo::websocket::is_control_opcode(header.op));
	riwo::ignore_unused(riwo::websocket::is_data_opcode(header.op));
	riwo::ignore_unused(riwo::websocket::is_valid_close_code(close_code));
	const auto close_payload = riwo::websocket::encode_close_payload(
		riwo::websocket::close_payload_view {close_code, input});
	if(close_payload)
	{
		const auto decoded = riwo::websocket::decode_close_payload(
			close_payload->buffer());
		if(not decoded or decoded->code != close_code or decoded->reason != input)
			std::abort();
	}

	std::vector<std::byte> destination(size);
	const uint64_t mask_offset = size > 2 ? data[2] : data[0];
	const auto copied = riwo::websocket::mask_copy(
		riwo::mutable_buffer(destination.data(), destination.size()),
		riwo::const_buffer(bytes.data(), bytes.size()), key, mask_offset);
	if(not copied or *copied != bytes.size())
		std::abort();
	if(size != 0)
	{
		std::vector<std::byte> too_small(size - 1, std::byte {0xa5});
		const auto before = too_small;
		const auto rejected = riwo::websocket::mask_copy(
			riwo::mutable_buffer(too_small.data(), too_small.size()),
			riwo::const_buffer(bytes.data(), bytes.size()), key, mask_offset);
		if(rejected or too_small != before)
			std::abort();
	}
	riwo::websocket::apply_mask(
		riwo::mutable_buffer(destination.data(), destination.size()), key, mask_offset);
	if(destination != bytes)
		std::abort();
	riwo::websocket::apply_mask(
		riwo::mutable_buffer(destination.data(), destination.size()), key, size);

	riwo::websocket::permessage_deflate_options options;
	options.server_no_context_takeover = (data[0] & 1U) != 0;
	options.client_no_context_takeover = (data[0] & 2U) != 0;
	options.offer_client_max_window_bits = (data[0] & 4U) != 0;
	options.server_max_window_bits = static_cast<uint8_t>(data[0]);
	options.client_max_window_bits = static_cast<uint8_t>(data[size - 1]);
	const auto extension = riwo::websocket::permessage_deflate_extension(options);
	riwo::ignore_unused(riwo::websocket::is_permessage_deflate_extension(extension));
	return 0;
}
