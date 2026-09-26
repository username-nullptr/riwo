// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/websocket/error.h>
#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/handshake.h>
#include <riwo/websocket/protocol/parser.h>
#include <riwo/websocket/detail/secure_random.h>

namespace ws = riwo::websocket;

static_assert(std::is_error_code_enum_v<ws::errc>);
static_assert(std::is_error_code_enum_v<ws::protocol_errc>);

namespace
{

[[nodiscard]] uint8_t octet(const riwo::const_buffer &buffer, size_t index)
{
	return static_cast<const uint8_t*>(buffer.data())[index];
}

void check_error(const riwo::error_code &actual, ws::protocol_errc expected)
{
	RIWO_TEST_CHECK_EQ(actual, ws::make_error_code(expected));
}

void check_error(const riwo::error_code &actual, ws::errc expected)
{
	RIWO_TEST_CHECK_EQ(actual, ws::make_error_code(expected));
}

void test_error_categories()
{
	const riwo::error_code stream_error = ws::errc::not_open;
	RIWO_TEST_CHECK_EQ(stream_error.category(), ws::error_category());
	RIWO_TEST_CHECK_EQ(std::string_view(stream_error.category().name()),
		"riwo::websocket");
	RIWO_TEST_CHECK(not stream_error.message().empty());
	RIWO_TEST_CHECK(stream_error == ws::errc::not_open);
	RIWO_TEST_CHECK(ws::errc::not_open == stream_error);
	RIWO_TEST_CHECK(stream_error != ws::errc::closed);

	const riwo::error_code protocol_error = ws::protocol_errc::invalid_utf8;
	RIWO_TEST_CHECK_EQ(protocol_error.category(), ws::protocol_error_category());
	RIWO_TEST_CHECK_EQ(std::string_view(protocol_error.category().name()),
		"riwo::websocket::protocol");
	RIWO_TEST_CHECK(not protocol_error.message().empty());
	RIWO_TEST_CHECK(protocol_error == ws::protocol_errc::invalid_utf8);
	RIWO_TEST_CHECK(ws::protocol_errc::invalid_utf8 == protocol_error);
	RIWO_TEST_CHECK(protocol_error != ws::protocol_errc::missing_mask);

#define X_MACRO(e,v,d) \
	{ \
		const riwo::error_code mapped = ws::errc::e; \
		RIWO_TEST_CHECK_EQ(mapped.value(), v); \
		RIWO_TEST_CHECK_EQ(mapped.message(), d); \
	}
	RIWO_WEBSOCKET_ERRC_TABLE
#undef X_MACRO
}

void test_secure_random_source()
{
	auto empty = ws::detail::secure_random_bytes(riwo::mutable_buffer {});
	RIWO_TEST_CHECK(empty.has_value());

	auto invalid = ws::detail::secure_random_bytes(
		riwo::mutable_buffer(nullptr, 1)
	);
	RIWO_TEST_CHECK(not invalid.has_value());
	RIWO_TEST_CHECK_EQ(invalid.error(),
		std::make_error_code(std::errc::invalid_argument));

	std::array<std::byte,32> bytes {};
	auto filled = ws::detail::secure_random_bytes(
		riwo::mutable_buffer(bytes.data(), bytes.size())
	);
	RIWO_TEST_CHECK(filled.has_value());
}

void test_opcode_and_close_code_helpers()
{
	RIWO_TEST_CHECK(ws::is_known_opcode(ws::opcode::continuation));
	RIWO_TEST_CHECK(ws::is_known_opcode(ws::opcode::pong));
	RIWO_TEST_CHECK(not ws::is_known_opcode(static_cast<ws::opcode>(0x03)));
	RIWO_TEST_CHECK(ws::is_control_opcode(ws::opcode::ping));
	RIWO_TEST_CHECK(not ws::is_control_opcode(ws::opcode::text));
	RIWO_TEST_CHECK(ws::is_data_opcode(ws::opcode::text));
	RIWO_TEST_CHECK(not ws::is_data_opcode(ws::opcode::continuation));

	RIWO_TEST_CHECK(ws::is_valid_close_code(1000));
	RIWO_TEST_CHECK(ws::is_valid_close_code(1014));
	RIWO_TEST_CHECK(ws::is_valid_close_code(3000));
	RIWO_TEST_CHECK(ws::is_valid_close_code(4999));
	RIWO_TEST_CHECK(not ws::is_valid_close_code(999));
	RIWO_TEST_CHECK(not ws::is_valid_close_code(1005));
	RIWO_TEST_CHECK(not ws::is_valid_close_code(1015));
	RIWO_TEST_CHECK(not ws::is_valid_close_code(2000));
	RIWO_TEST_CHECK(not ws::is_valid_close_code(5000));

	RIWO_TEST_CHECK_EQ(ws::close_code_for(ws::protocol_errc::invalid_utf8),
		ws::close_code::invalid_payload);
	RIWO_TEST_CHECK_EQ(ws::close_code_for(ws::protocol_errc::frame_too_large),
		ws::close_code::message_too_big);
	RIWO_TEST_CHECK_EQ(ws::close_code_for(ws::protocol_errc::missing_mask),
		ws::close_code::protocol_error);
}

void test_frame_header_encoding()
{
	ws::frame_codec_config server_config;
	server_config.local_role = ws::role::server;

	auto encoded = ws::encode_frame_header({
		.fin = true,
		.op = ws::opcode::binary,
		.payload_size = 125,
	}, server_config);
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK_EQ(encoded->size, 2);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 0), 0x82);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 1), 125);

	encoded = ws::encode_frame_header({
		.fin = true,
		.op = ws::opcode::binary,
		.payload_size = 126,
	}, server_config);
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK_EQ(encoded->size, 4);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 1), 126);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 2), 0);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 3), 126);

	server_config.max_frame_size = 0;
	encoded = ws::encode_frame_header({
		.fin = true,
		.op = ws::opcode::binary,
		.payload_size = 0x10000,
	}, server_config);
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK_EQ(encoded->size, 10);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 1), 127);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 7), 1);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 8), 0);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 9), 0);

	const ws::masking_key key {{
		std::byte {0x12}, std::byte {0x34}, std::byte {0x56}, std::byte {0x78}
	}};
	encoded = ws::encode_frame_header({
		.fin = true,
		.rsv = ws::reserved_bit::rsv1,
		.op = ws::opcode::text,
		.payload_size = 5,
		.mask = key,
	}, ws::frame_codec_config {
		.local_role = ws::role::client,
		.max_frame_size = 16 * 1024 * 1024,
		.allowed_rsv = ws::reserved_bit::rsv1,
	});
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK_EQ(encoded->size, 6);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 0), 0xC1);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 1), 0x85);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 2), 0x12);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 5), 0x78);
}

void test_frame_header_errors()
{
	ws::frame_codec_config server_config;
	server_config.local_role = ws::role::server;

	auto result = ws::encode_frame_header({
		.op = static_cast<ws::opcode>(0x03),
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::reserved_opcode);

	result = ws::encode_frame_header({
		.rsv = ws::reserved_bit::rsv1,
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::unexpected_rsv);

	result = ws::encode_frame_header({
		.fin = false,
		.op = ws::opcode::ping,
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::fragmented_control_frame);

	result = ws::encode_frame_header({
		.op = ws::opcode::close,
		.payload_size = 126,
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::ctrl_payload_too_large);

	result = ws::encode_frame_header({
		.payload_size = server_config.max_frame_size + 1,
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::frame_too_large);

	result = ws::encode_frame_header({}, ws::frame_codec_config {
		.local_role = ws::role::client,
	});
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::missing_mask);

	result = ws::encode_frame_header({
		.mask = ws::masking_key {},
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::unexpected_mask);

	server_config.max_frame_size = 0;
	result = ws::encode_frame_header({
		.payload_size = std::numeric_limits<uint64_t>::max(),
	}, server_config);
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::invalid_64bit_length);
}

void test_masking()
{
	const ws::masking_key key {{
		std::byte {0x37}, std::byte {0xFA}, std::byte {0x21}, std::byte {0x3D}
	}};
	std::array<std::byte,5> payload {
		std::byte {'H'}, std::byte {'e'}, std::byte {'l'}, std::byte {'l'}, std::byte {'o'}
	};
	ws::apply_mask(riwo::mutable_buffer(payload.data(), payload.size()), key);
	RIWO_TEST_CHECK_EQ(std::to_integer<uint8_t>(payload[0]), 0x7F);
	RIWO_TEST_CHECK_EQ(std::to_integer<uint8_t>(payload[1]), 0x9F);
	RIWO_TEST_CHECK_EQ(std::to_integer<uint8_t>(payload[2]), 0x4D);
	RIWO_TEST_CHECK_EQ(std::to_integer<uint8_t>(payload[3]), 0x51);
	RIWO_TEST_CHECK_EQ(std::to_integer<uint8_t>(payload[4]), 0x58);
	ws::apply_mask(riwo::mutable_buffer(payload.data(), payload.size()), key);
	RIWO_TEST_CHECK(std::memcmp(payload.data(), "Hello", payload.size()) == 0);

	std::array<std::byte,3> source {
		std::byte {0x10}, std::byte {0x20}, std::byte {0x30}
	};
	std::array<std::byte,3> destination {};
	auto copied = ws::mask_copy (
		riwo::mutable_buffer(destination.data(), destination.size()),
		riwo::const_buffer(source.data(), source.size()), key, 3
	);
	RIWO_TEST_CHECK(copied.has_value());
	RIWO_TEST_CHECK_EQ(*copied, source.size());
	RIWO_TEST_CHECK_EQ(destination[0], source[0] ^ key.bytes[3]);
	RIWO_TEST_CHECK_EQ(destination[1], source[1] ^ key.bytes[0]);
	RIWO_TEST_CHECK_EQ(destination[2], source[2] ^ key.bytes[1]);

	std::array<std::byte,2> too_small {std::byte {0xAA}, std::byte {0xBB}};
	copied = ws::mask_copy (
		riwo::mutable_buffer(too_small.data(), too_small.size()),
		riwo::const_buffer(source.data(), source.size()), key
	);
	RIWO_TEST_CHECK(not copied.has_value());
	RIWO_TEST_CHECK_EQ(copied.error(),
		std::make_error_code(std::errc::no_buffer_space));
	RIWO_TEST_CHECK_EQ(too_small[0], std::byte {0xAA});
	RIWO_TEST_CHECK_EQ(too_small[1], std::byte {0xBB});
}

void test_close_payload()
{
	auto encoded = ws::encode_close_payload(ws::close_frame {
		ws::close_code::normal_closure, "done"
	});
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK_EQ(encoded->size, 6);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 0), 0x03);
	RIWO_TEST_CHECK_EQ(octet(encoded->buffer(), 1), 0xE8);

	auto decoded = ws::decode_close_payload(encoded->buffer());
	RIWO_TEST_CHECK(decoded.has_value());
	RIWO_TEST_CHECK(decoded->code.has_value());
	RIWO_TEST_CHECK_EQ(*decoded->code, 1000);
	RIWO_TEST_CHECK_EQ(decoded->reason, "done");

	decoded = ws::decode_close_payload(riwo::const_buffer {});
	RIWO_TEST_CHECK(decoded.has_value());
	RIWO_TEST_CHECK(not decoded->code.has_value());
	RIWO_TEST_CHECK(decoded->reason.empty());

	const std::array<std::byte,1> one_byte {std::byte {0x03}};
	decoded = ws::decode_close_payload({one_byte.data(), one_byte.size()});
	RIWO_TEST_CHECK(not decoded.has_value());
	check_error(decoded.error(), ws::protocol_errc::invalid_close_payload);

	const std::array<std::byte,2> forbidden_code {
		std::byte {0x03}, std::byte {0xED}
	};
	decoded = ws::decode_close_payload({forbidden_code.data(), forbidden_code.size()});
	RIWO_TEST_CHECK(not decoded.has_value());
	check_error(decoded.error(), ws::protocol_errc::invalid_close_payload);

	encoded = ws::encode_close_payload(ws::close_payload_view {
		.code = riwo::nullopt,
		.reason = "reason without code",
	});
	RIWO_TEST_CHECK(not encoded.has_value());
	check_error(encoded.error(), ws::protocol_errc::invalid_close_payload);

	const std::string overlong_utf8("\xC0\x80", 2);
	encoded = ws::encode_close_payload(ws::close_payload_view {
		.code = 1000,
		.reason = overlong_utf8,
	});
	RIWO_TEST_CHECK(not encoded.has_value());
	check_error(encoded.error(), ws::protocol_errc::invalid_utf8);

	const std::string too_long(124, 'a');
	encoded = ws::encode_close_payload(ws::close_payload_view {
		.code = 1000,
		.reason = too_long,
	});
	RIWO_TEST_CHECK(not encoded.has_value());
	check_error(encoded.error(), ws::protocol_errc::ctrl_payload_too_large);

	const std::array<std::byte,5> surrogate_utf8 {
		std::byte {0x03}, std::byte {0xE8},
		std::byte {0xED}, std::byte {0xA0}, std::byte {0x80}
	};
	decoded = ws::decode_close_payload({surrogate_utf8.data(), surrogate_utf8.size()});
	RIWO_TEST_CHECK(not decoded.has_value());
	check_error(decoded.error(), ws::protocol_errc::invalid_utf8);
}

void test_opening_handshake_round_trip()
{
	const std::string nonce_text = "the sample nonce";
	std::array<std::byte,16> nonce {};
	std::memcpy(nonce.data(), nonce_text.data(), nonce.size());
	auto client_key = ws::make_client_key(nonce);
	RIWO_TEST_CHECK(client_key.has_value());
	RIWO_TEST_CHECK_EQ(*client_key, "dGhlIHNhbXBsZSBub25jZQ==");

	auto accept_key = ws::make_accept_key(*client_key);
	RIWO_TEST_CHECK(accept_key.has_value());
	RIWO_TEST_CHECK_EQ(*accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

	ws::extension compression;
	compression.name = "permessage-deflate";
	compression.parameters = {
		{.name = "client_max_window_bits"},
		{.name = "mode", .value = std::string("fast")},
	};
	ws::opening_request request {
		.key = *client_key,
		.subprotocols = {"chat", "superchat"},
		.extensions = {compression},
	};

	auto request_headers = ws::make_opening_request_headers(request);
	RIWO_TEST_CHECK(request_headers.has_value());
	RIWO_TEST_CHECK_EQ(request_headers->at("Connection").to_string(), "Upgrade");
	RIWO_TEST_CHECK_EQ(request_headers->at("Upgrade").to_string(), "websocket");
	RIWO_TEST_CHECK_EQ(request_headers->at("Sec-WebSocket-Version").to_string(), "13");
	RIWO_TEST_CHECK_EQ(request_headers->at("Sec-WebSocket-Protocol").to_string(),
		"chat, superchat");
	RIWO_TEST_CHECK_EQ(request_headers->at("Sec-WebSocket-Extensions").to_string(),
		"permessage-deflate; client_max_window_bits; mode=fast");

	(*request_headers)["Connection"] = "keep-alive, uPgRaDe";
	(*request_headers)["Upgrade"] = "WebSocket";
	(*request_headers)["Sec-WebSocket-Extensions"] =
		"permessage-deflate; client_max_window_bits; mode=\"fa\\st\"";
	auto parsed_request = ws::parse_opening_request(riwo::http::method::get,
		riwo::http::version::v11, *request_headers);
	RIWO_TEST_CHECK(parsed_request.has_value());
	RIWO_TEST_CHECK_EQ(parsed_request->key, *client_key);
	RIWO_TEST_CHECK_EQ(parsed_request->subprotocols.size(), size_t {2});
	RIWO_TEST_CHECK_EQ(parsed_request->subprotocols[1], "superchat");
	RIWO_TEST_CHECK_EQ(parsed_request->extensions.size(), size_t {1});
	RIWO_TEST_CHECK_EQ(parsed_request->extensions[0].parameters.size(), size_t {2});
	RIWO_TEST_CHECK(not parsed_request->extensions[0].parameters[0].value);
	RIWO_TEST_CHECK_EQ(*parsed_request->extensions[0].parameters[1].value, "fast");

	ws::opening_response response {
		.subprotocol = std::string("chat"),
		.extensions = {compression},
	};
	auto response_headers = ws::make_opening_response_headers(*parsed_request, response);
	RIWO_TEST_CHECK(response_headers.has_value());
	RIWO_TEST_CHECK_EQ(response_headers->at("Sec-WebSocket-Accept").to_string(),
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
	auto parsed_response = ws::parse_opening_response (
		riwo::http::status::switching_protocols,
		*response_headers, *parsed_request
	);
	RIWO_TEST_CHECK(parsed_response.has_value());
	RIWO_TEST_CHECK_EQ(parsed_response->subprotocol.value_or(""), "chat");
	RIWO_TEST_CHECK_EQ(parsed_response->extensions.size(), size_t {1});
}

void test_opening_handshake_errors()
{
	ws::opening_request request {
		.key = "dGhlIHNhbXBsZSBub25jZQ==",
		.subprotocols = {"chat", "superchat"},
	};
	auto generated = ws::make_opening_request_headers(request);
	RIWO_TEST_CHECK(generated.has_value());

	auto parsed = ws::parse_opening_request(riwo::http::method::post,
		riwo::http::version::v11, *generated);
	RIWO_TEST_CHECK(not parsed.has_value());
	check_error(parsed.error(), ws::errc::invalid_upgrade);

	auto headers = *generated;
	headers["Sec-WebSocket-Version"] = "12";
	parsed = ws::parse_opening_request(riwo::http::method::get,
		riwo::http::version::v11, headers);
	RIWO_TEST_CHECK(not parsed.has_value());
	check_error(parsed.error(), ws::errc::unsupported_version);

	headers = *generated;
	headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ=A";
	parsed = ws::parse_opening_request(riwo::http::method::get,
		riwo::http::version::v11, headers);
	RIWO_TEST_CHECK(not parsed.has_value());
	check_error(parsed.error(), ws::errc::invalid_upgrade);

	headers = *generated;
	headers["Sec-WebSocket-Protocol"] = "chat, chat";
	parsed = ws::parse_opening_request(riwo::http::method::get,
		riwo::http::version::v11, headers);
	RIWO_TEST_CHECK(not parsed.has_value());
	check_error(parsed.error(), ws::errc::invalid_upgrade);

	headers = *generated;
	headers["Sec-WebSocket-Extensions"] = "permessage-deflate; mode=\"not valid\"";
	parsed = ws::parse_opening_request(riwo::http::method::get,
		riwo::http::version::v11, headers);
	RIWO_TEST_CHECK(not parsed.has_value());
	check_error(parsed.error(), ws::errc::invalid_upgrade);

	request.subprotocols = {"chat", "chat"};
	generated = ws::make_opening_request_headers(request);
	RIWO_TEST_CHECK(not generated.has_value());
	check_error(generated.error(), ws::errc::invalid_upgrade);

	request.subprotocols = {"chat", "superchat"};
	auto response_headers = ws::make_opening_response_headers(request, {});
	RIWO_TEST_CHECK(response_headers.has_value());
	auto response = ws::parse_opening_response(riwo::http::status::ok,
		*response_headers, request);
	RIWO_TEST_CHECK(not response.has_value());
	check_error(response.error(), ws::errc::handshake_rejected);

	headers = *response_headers;
	headers["Sec-WebSocket-Accept"] = "incorrect";
	response = ws::parse_opening_response(riwo::http::status::switching_protocols,
		headers, request);
	RIWO_TEST_CHECK(not response.has_value());
	check_error(response.error(), ws::errc::invalid_accept_key);

	headers = *response_headers;
	headers["Sec-WebSocket-Protocol"] = "other";
	response = ws::parse_opening_response(riwo::http::status::switching_protocols,
		headers, request);
	RIWO_TEST_CHECK(not response.has_value());
	check_error(response.error(), ws::errc::unsupported_subprotocol);

	headers = *response_headers;
	headers["Sec-WebSocket-Protocol"] = "chat, superchat";
	response = ws::parse_opening_response(riwo::http::status::switching_protocols,
		headers, request);
	RIWO_TEST_CHECK(not response.has_value());
	check_error(response.error(), ws::errc::invalid_upgrade);
}

void test_incremental_frame_parser()
{
	ws::frame_parser parser(ws::frame_codec_config {
		.local_role = ws::role::server,
	});
	std::array<std::byte,1> chunk1 {std::byte {0x81}};
	std::array<std::byte,3> chunk2 {
		std::byte {0x85}, std::byte {0x37}, std::byte {0xFA}
	};
	std::array<std::byte,4> chunk3 {
		std::byte {0x21}, std::byte {0x3D}, std::byte {0x7F}, std::byte {0x9F}
	};
	std::array<std::byte,5> chunk4 {
		std::byte {0x4D}, std::byte {0x51}, std::byte {0x58},
		std::byte {0xAA}, std::byte {0xBB}
	};

	auto result = parser.parse({chunk1.data(), chunk1.size()});
	RIWO_TEST_CHECK(result.has_value());
	RIWO_TEST_CHECK_EQ(result->consumed, size_t {1});
	RIWO_TEST_CHECK(not result->header_ready);
	RIWO_TEST_CHECK(not result->frame_finished);

	result = parser.parse({chunk2.data(), chunk2.size()});
	RIWO_TEST_CHECK(result.has_value());
	RIWO_TEST_CHECK_EQ(result->consumed, chunk2.size());
	RIWO_TEST_CHECK(not result->header_ready);

	result = parser.parse({chunk3.data(), chunk3.size()});
	RIWO_TEST_CHECK(result.has_value());
	RIWO_TEST_CHECK(result->header_ready);
	RIWO_TEST_CHECK(not result->frame_finished);
	RIWO_TEST_CHECK_EQ(result->consumed, chunk3.size());
	RIWO_TEST_CHECK_EQ(result->payload.size(), size_t {2});
	RIWO_TEST_CHECK_EQ(result->payload_offset, uint64_t {0});
	RIWO_TEST_CHECK_EQ(parser.payload_remaining(), uint64_t {3});
	RIWO_TEST_CHECK_EQ(parser.header().op, ws::opcode::text);
	RIWO_TEST_CHECK_EQ(parser.header().payload_size, uint64_t {5});
	RIWO_TEST_CHECK(parser.header().mask.has_value());
	ws::apply_mask(result->payload, *parser.header().mask, result->payload_offset);
	RIWO_TEST_CHECK(std::memcmp(chunk3.data() + 2, "He", 2) == 0);

	result = parser.parse({chunk4.data(), chunk4.size()});
	RIWO_TEST_CHECK(result.has_value());
	RIWO_TEST_CHECK(not result->header_ready);
	RIWO_TEST_CHECK(result->frame_finished);
	RIWO_TEST_CHECK_EQ(result->consumed, size_t {3});
	RIWO_TEST_CHECK_EQ(result->payload.size(), size_t {3});
	RIWO_TEST_CHECK_EQ(result->payload_offset, uint64_t {2});
	RIWO_TEST_CHECK_EQ(parser.payload_remaining(), uint64_t {0});
	ws::apply_mask(result->payload, *parser.header().mask, result->payload_offset);
	RIWO_TEST_CHECK(std::memcmp(chunk4.data(), "llo", 3) == 0);
}

template <size_t Size>
void check_parser_error(std::array<std::byte,Size> wire,
	ws::frame_codec_config config, ws::protocol_errc expected)
{
	ws::frame_parser parser(config);
	auto result = parser.parse({wire.data(), wire.size()});
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), expected);
	RIWO_TEST_CHECK(parser.failed());
	check_error(parser.last_error(), expected);
}

void test_frame_parser_errors()
{
	check_parser_error(std::array {
		std::byte {0x83}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::reserved_opcode);

	check_parser_error(std::array {
		std::byte {0xC2}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::unexpected_rsv);

	check_parser_error(std::array {
		std::byte {0x82}, std::byte {0x80},
		std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::unexpected_mask);

	check_parser_error(std::array {
		std::byte {0x82}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::server},
		ws::protocol_errc::missing_mask);

	check_parser_error(std::array {
		std::byte {0x82}, std::byte {0x7E}, std::byte {0x00}, std::byte {0x7D}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::noncanonical_length);

	check_parser_error(std::array {
		std::byte {0x82}, std::byte {0x7F}, std::byte {0x80}, std::byte {0x00},
		std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0x00},
		std::byte {0x00}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::invalid_64bit_length);

	check_parser_error(std::array {
		std::byte {0x09}, std::byte {0x00}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::fragmented_control_frame);

	check_parser_error(std::array {
		std::byte {0x88}, std::byte {0x7E}, std::byte {0x00}, std::byte {0x7E}
	}, ws::frame_codec_config {.local_role = ws::role::client},
		ws::protocol_errc::ctrl_payload_too_large);

	check_parser_error(std::array {
		std::byte {0x82}, std::byte {0x7E}, std::byte {0x04}, std::byte {0x00}
	}, ws::frame_codec_config {
		.local_role = ws::role::client,
		.max_frame_size = 1000,
	}, ws::protocol_errc::frame_too_large);
}

void parse_empty_frame(ws::frame_parser &parser, uint8_t first)
{
	std::array<std::byte,2> wire {
		static_cast<std::byte>(first), std::byte {0x00}
	};
	auto result = parser.parse({wire.data(), wire.size()});
	RIWO_TEST_CHECK(result.has_value());
	RIWO_TEST_CHECK(result->header_ready);
	RIWO_TEST_CHECK(result->frame_finished);
	RIWO_TEST_CHECK_EQ(result->consumed, wire.size());
}

void test_frame_fragmentation_state()
{
	ws::frame_parser parser(ws::frame_codec_config {
		.local_role = ws::role::client,
	});
	parse_empty_frame(parser, 0x01);
	parse_empty_frame(parser, 0x89);

	std::array<std::byte,2> data_during_fragmentation {
		std::byte {0x82}, std::byte {0x00}
	};
	auto result = parser.parse({data_during_fragmentation.data(),
		data_during_fragmentation.size()});
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::data_during_fragmentation);

	std::array<std::byte,2> continuation {
		std::byte {0x80}, std::byte {0x00}
	};
	result = parser.parse({continuation.data(), continuation.size()});
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::data_during_fragmentation);

	parser.reset();
	result = parser.parse({continuation.data(), continuation.size()});
	RIWO_TEST_CHECK(not result.has_value());
	check_error(result.error(), ws::protocol_errc::unexpected_continuation);

	parser.reset();
	parse_empty_frame(parser, 0x01);
	parse_empty_frame(parser, 0x00);
	parse_empty_frame(parser, 0x80);
	parse_empty_frame(parser, 0x82);
	RIWO_TEST_CHECK(not parser.failed());
}

void test_frame_codec_chunk_corpus()
{
	constexpr std::array<size_t,9> payload_sizes {
		0, 1, 2, 7, 125, 126, 127, 65535, 65536,
	};
	constexpr std::array<size_t,8> chunk_sizes {
		1, 2, 3, 5, 7, 13, 127, 4096,
	};
	const ws::masking_key mask {{
		std::byte {0x12}, std::byte {0x34},
		std::byte {0x56}, std::byte {0x78},
	}};

	for( const auto sender_role : {ws::role::client, ws::role::server} )
	{
		for( const auto payload_size : payload_sizes )
		{
			std::vector<std::byte> payload(payload_size);
			for( size_t index = 0; index < payload.size(); ++index )
				payload[index] = static_cast<std::byte>((index * 37U + 11U) & 0xFFU);

			ws::frame_header header {
				.fin = true,
				.op = ws::opcode::binary,
				.payload_size = payload.size(),
				.mask = sender_role == ws::role::client ?
					riwo::optional<ws::masking_key>(mask) : riwo::nullopt,
			};
			auto encoded = ws::encode_frame_header(header, ws::frame_codec_config {
				.local_role = sender_role,
				.max_frame_size = 0,
			});
			RIWO_TEST_CHECK(encoded.has_value());

			std::vector<std::byte> wire(encoded->size + payload.size());
			std::memcpy(wire.data(), encoded->buffer().data(), encoded->size);
			if( not payload.empty() )
				std::memcpy(wire.data() + encoded->size, payload.data(), payload.size());
			if( header.mask and not payload.empty() )
			{
				ws::apply_mask(riwo::mutable_buffer(
					wire.data() + encoded->size, payload.size()), *header.mask);
			}

			for( const auto chunk_size : chunk_sizes )
			{
				ws::frame_parser parser(ws::frame_codec_config {
					.local_role = sender_role == ws::role::client ?
						ws::role::server : ws::role::client,
					.max_frame_size = 0,
				});
				auto input = wire;
				std::vector<std::byte> decoded;
				size_t offset = 0;
				bool header_ready = false;
				bool frame_finished = false;

				while( offset < input.size() )
				{
					const auto available = std::min(chunk_size, input.size() - offset);
					auto result = parser.parse(riwo::mutable_buffer(
						input.data() + offset, available));
					RIWO_TEST_CHECK(result.has_value());
					RIWO_TEST_CHECK(result->consumed > 0);
					if( result->header_ready )
						header_ready = true;
					if( result->payload.size() > 0 )
					{
						if( parser.header().mask )
						{
							ws::apply_mask(result->payload, *parser.header().mask,
								result->payload_offset);
						}
						auto *begin = static_cast<const std::byte*>(
							result->payload.data());
						decoded.insert(decoded.end(), begin,
							begin + result->payload.size());
					}
					frame_finished = frame_finished or result->frame_finished;
					offset += result->consumed;
				}

				RIWO_TEST_CHECK(header_ready);
				RIWO_TEST_CHECK(frame_finished);
				RIWO_TEST_CHECK_EQ(decoded, payload);
				RIWO_TEST_CHECK_EQ(parser.header().payload_size,
					static_cast<uint64_t>(payload.size()));
				RIWO_TEST_CHECK(not parser.failed());
			}
		}
	}
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"error categories", test_error_categories},
		{"secure random source", test_secure_random_source},
		{"opcode and close code helpers", test_opcode_and_close_code_helpers},
		{"frame header encoding", test_frame_header_encoding},
		{"frame header errors", test_frame_header_errors},
		{"masking", test_masking},
		{"Close payload", test_close_payload},
		{"opening handshake round trip", test_opening_handshake_round_trip},
		{"opening handshake errors", test_opening_handshake_errors},
		{"incremental frame parser", test_incremental_frame_parser},
		{"frame parser errors", test_frame_parser_errors},
		{"frame fragmentation state", test_frame_fragmentation_state},
		{"frame codec chunk corpus", test_frame_codec_chunk_corpus},
	});
}
