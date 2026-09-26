// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/handshake.h>
#include <riwo/websocket/protocol/parser.h>
#include <iostream>

namespace ws = riwo::websocket;

int main()
{
	const auto fail = [](std::string_view operation, const riwo::error_code &error) {
		std::cerr << operation << " failed: " << error.message() << '\n';
		return 1;
	};
	const std::string nonce_text = "the sample nonce";
	std::array<std::byte,16> nonce {};

	std::memcpy(nonce.data(), nonce_text.data(), nonce.size());
	auto client_key = ws::make_client_key(nonce);

	if( not client_key )
		return fail("Client key generation", client_key.error());

	ws::opening_request client_request {
		.key = *client_key,
		.subprotocols = {"chat"},
	};
	auto request_headers = ws::make_opening_request_headers(client_request);
	if( not request_headers )
		return fail("Opening request generation", request_headers.error());

	auto server_request = ws::parse_opening_request (
		riwo::http::method::get, riwo::http::version::v11,
		*request_headers
	);
	if( not server_request )
		return fail("Opening request parsing", server_request.error());

	auto response_headers = ws::make_opening_response_headers (
		*server_request, ws::opening_response {.subprotocol = std::string("chat")}
	);
	if( not response_headers )
		return fail("Opening response generation", response_headers.error());

	auto client_response = ws::parse_opening_response (
		riwo::http::status::switching_protocols,
		*response_headers, client_request
	);
	if( not client_response )
		return fail("Opening response parsing", client_response.error());

	const ws::masking_key mask {{
		std::byte {0x37}, std::byte {0xFA}, std::byte {0x21}, std::byte {0x3D}
	}};
	const std::string text = "Hello";
	std::vector<std::byte> masked(text.size());

	auto masked_size = ws::mask_copy (
		riwo::mutable_buffer(masked.data(), masked.size()),
		riwo::const_buffer(text), mask
	);
	if( not masked_size )
		return fail("Payload masking", masked_size.error());

	auto frame_head = ws::encode_frame_header (
		ws::frame_header {
			.fin = true,
			.op = ws::opcode::text,
			.payload_size = masked.size(),
			.mask = mask,
		},
		ws::frame_codec_config {
			.local_role = ws::role::client
		}
	);
	if( not frame_head )
		return fail("Frame generation", frame_head.error());

	std::vector<std::byte> wire;
	wire.reserve(frame_head->size + masked.size());

	wire.insert(wire.end(), frame_head->storage.begin(),
		frame_head->storage.begin() + frame_head->size
	);
	wire.insert(wire.end(), masked.begin(), masked.end());

	ws::frame_parser parser(ws::frame_codec_config {
		.local_role = ws::role::server,
	});
	auto frame = parser.parse(riwo::mutable_buffer(wire.data(), wire.size()));
	if( not frame )
		return fail("Frame parsing", frame.error());

	if( not frame->header_ready or not frame->frame_finished or not parser.header().mask )
	{
		std::cerr << "Frame parsing produced an incomplete result\n";
		return 1;
	}
	ws::apply_mask(frame->payload, *parser.header().mask, frame->payload_offset);

	const std::string decoded (
		static_cast<const char*>(frame->payload.data()),
		frame->payload.size()
	);
	std::cout << "Client key: " << *client_key << '\n';
	std::cout << "Accept key: "
		<< response_headers->at("Sec-WebSocket-Accept").to_string() << '\n';

	std::cout << "Subprotocol: "
		<< client_response->subprotocol.value_or("(none)") << '\n';

	std::cout << "Decoded frame: " << decoded << '\n';
	return decoded == text ? 0 : 1;
}
