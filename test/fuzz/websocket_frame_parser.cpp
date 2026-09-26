// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/parser.h>

namespace
{

void generated_frame_round_trip(const uint8_t *data, size_t size)
{
	if(size < 4)
		return;

	namespace ws = riwo::websocket;
	const auto sender_role = (data[0] & 1U) != 0 ? ws::role::client : ws::role::server;
	const auto receiver_role = sender_role == ws::role::client ?
		ws::role::server : ws::role::client;
	const auto allowed_rsv = ws::reserved_bits(
		static_cast<ws::reserved_bit>((data[0] >> 1U) & 0x07U));
	ws::masking_key key;
	for(size_t index = 0; index < key.bytes.size(); ++index)
		key.bytes[index] = std::byte {data[index]};

	std::vector<std::byte> payload(size - 4);
	if(not payload.empty())
		std::memcpy(payload.data(), data + 4, payload.size());
	ws::frame_header header {
		.fin = (data[1] & 1U) != 0,
		.rsv = allowed_rsv,
		.op = ws::opcode::binary,
		.payload_size = payload.size(),
		.mask = sender_role == ws::role::client ?
			riwo::optional<ws::masking_key>(key) : riwo::nullopt,
	};
	const ws::frame_codec_config sender_config {
		.local_role = sender_role,
		.max_frame_size = payload.size() + 1,
		.allowed_rsv = allowed_rsv,
	};
	auto encoded = ws::encode_frame_header(header, sender_config);
	if(not encoded)
		std::abort();

	std::vector<std::byte> wire(encoded->size + payload.size());
	std::memcpy(wire.data(), encoded->storage.data(), encoded->size);
	if(not payload.empty())
		std::memcpy(wire.data() + encoded->size, payload.data(), payload.size());
	if(header.mask)
	{
		ws::apply_mask(riwo::mutable_buffer(
			wire.data() + encoded->size, payload.size()), *header.mask);
	}

	ws::frame_parser parser(ws::frame_codec_config {
		.local_role = receiver_role,
		.max_frame_size = payload.size() + 1,
		.allowed_rsv = allowed_rsv,
	});
	std::vector<std::byte> recovered;
	recovered.reserve(payload.size());
	size_t offset = 0;
	bool finished = false;
	while(offset < wire.size())
	{
		const auto requested = size_t(data[offset % size] % 17U) + 1;
		const auto available = std::min(requested, wire.size() - offset);
		auto parsed = parser.parse(riwo::mutable_buffer(
			wire.data() + offset, available));
		if(not parsed or parsed->consumed == 0 or parsed->consumed > available)
			std::abort();
		if(parsed->payload.size() != 0)
		{
			if(parser.header().mask)
			{
				ws::apply_mask(parsed->payload, *parser.header().mask,
					parsed->payload_offset);
			}
			const auto *first = static_cast<const std::byte*>(parsed->payload.data());
			recovered.insert(recovered.end(), first, first + parsed->payload.size());
		}
		offset += parsed->consumed;
		finished = parsed->frame_finished;
	}
	if(not finished)
		std::abort();
	if(parser.failed() or parser.payload_remaining() != 0)
		std::abort();
	if(parser.header().fin != header.fin or parser.header().rsv != header.rsv or
		parser.header().op != header.op or parser.header().payload_size != payload.size())
		std::abort();
	if(recovered != payload)
		std::abort();
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size < 3 )
		return 0;
	generated_frame_round_trip(data, size);

	riwo::websocket::frame_codec_config config;
	config.local_role = (data[0] & 1) ?
		riwo::websocket::role::client : riwo::websocket::role::server;
	config.max_frame_size = (data[1] & 1) ? 0 : 1024 * 1024;
	config.allowed_rsv = riwo::websocket::reserved_bits(
		static_cast<riwo::websocket::reserved_bit>(data[1] & 0x07));
	riwo::websocket::frame_parser parser(config);

	std::vector<std::byte> input(size - 2);
	std::memcpy(input.data(), data + 2, input.size());
	size_t offset = 0;
	while( offset < input.size() )
	{
		const auto chunk_size = size_t(data[(offset + 2) % size] % 31) + 1;
		const auto available = std::min(chunk_size, input.size() - offset);
		auto parsed = parser.parse(riwo::mutable_buffer(
			input.data() + offset, available));
		if(not parsed)
		{
			if(not parser.failed() or parser.last_error() != parsed.error())
				std::abort();
			auto repeated = parser.parse(riwo::mutable_buffer(
				input.data() + offset, available));
			if(repeated or repeated.error() != parsed.error())
				std::abort();
			parser.reset();
			if(parser.failed() or parser.last_error() or parser.payload_remaining() != 0)
				std::abort();
			break;
		}
		if(parsed->consumed == 0)
			break;
		offset += parsed->consumed;
	}
	riwo::ignore_unused(parser.header());
	riwo::ignore_unused(parser.payload_remaining());
	riwo::ignore_unused(parser.config());
	return 0;
}
