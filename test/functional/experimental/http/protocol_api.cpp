// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/experimental/http/protocol.h>
#include <cstring>

namespace
{

namespace http = riwo::experimental::http;

std::vector<std::byte> bytes(std::string_view value)
{
	std::vector<std::byte> result(value.size());
	std::memcpy(result.data(), value.data(), value.size());
	return result;
}

std::string drain(http::concepts::protocol_engine auto &engine,
	size_t chunk = 7)
{
	std::string result;
	std::vector<std::byte> buffer(chunk);
	while( engine.wants_write() )
	{
		auto produced = engine.produce(riwo::buffer(buffer));
		RIWO_TEST_CHECK(produced.has_value());
		result.append(reinterpret_cast<const char *>(buffer.data()),
			produced->produced);
		if( produced->produced == 0 and not produced->has_more ) break;
	}
	return result;
}

void h1_codec_fragmentation()
{
	http::request_head head;
	head.method = http::method::post;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/upload?q=1";
	head.headers.add("X-Test", "one");
	auto encoded = http::v1::serialize_request_head(head,
		http::body_spec::known(3), http::protocol::v1_1);
	RIWO_TEST_CHECK(encoded.has_value());
	RIWO_TEST_CHECK(encoded->wire.starts_with("POST /upload?q=1 HTTP/1.1\r\n"));
	RIWO_TEST_CHECK(encoded->framing == http::v1::body_framing::content_length);

	for(size_t split=0; split<=encoded->wire.size(); ++split)
	{
		http::v1::request_parser parser;
		auto first = parser.ingest(riwo::buffer(
			std::string_view(encoded->wire).substr(0, split)));
		RIWO_TEST_CHECK(first.has_value());
		if( split != encoded->wire.size() )
		{
			auto second = parser.ingest(riwo::buffer(
				std::string_view(encoded->wire).substr(split)));
			RIWO_TEST_CHECK(second.has_value());
			RIWO_TEST_CHECK(second->head.has_value());
			RIWO_TEST_CHECK_EQ(second->head->head.target, "/upload?q=1");
			RIWO_TEST_CHECK_EQ(second->head->content_length, 3U);
		}
		else RIWO_TEST_CHECK(first->head.has_value());
	}

	http::v1::chunk_encoder encoder;
	auto chunk = encoder.encode(riwo::buffer(std::string_view("abc")));
	RIWO_TEST_CHECK(chunk.has_value());
	auto last = encoder.finish();
	RIWO_TEST_CHECK(last.has_value());
	http::v1::chunk_decoder decoder;
	const auto wire = *chunk + *last;
	std::string decoded;
	size_t offset = 0;
	bool ended = false;
	while( offset < wire.size() )
	{
		auto value = decoder.ingest(riwo::buffer(
			std::string_view(wire).substr(offset, 1)));
		RIWO_TEST_CHECK(value.has_value());
		offset += value->consumed;
		decoded.append(reinterpret_cast<const char *>(value->data.data()),
			value->data.size());
		ended = ended or value->message_end;
	}
	RIWO_TEST_CHECK_EQ(decoded, "abc");
	RIWO_TEST_CHECK(ended);
	RIWO_TEST_CHECK(decoder.complete());
}

void h1_manual_engine_loop()
{
	http::v1::connection_engine client({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1
	});
	http::v1::connection_engine server({
		.role = http::protocol_role::server,
		.version = http::protocol::v1_1
	});

	http::request_head request;
	request.method = http::method::get;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/";
	RIWO_TEST_CHECK(client.submit(http::open_request_action{
		.stream = 9, .head = request, .body = http::body_spec::empty()
	}).has_value());
	const auto request_wire = drain(client, 1);
	http::stream_token_t server_stream = 0;
	bool request_end = false;
	auto server_result = server.ingest(riwo::buffer(request_wire),
	[&](const http::protocol_event_t &event) {
		if( auto head = std::get_if<http::request_head_event>(&event) )
			server_stream = head->stream;
		if( std::holds_alternative<http::stream_end_event>(event) )
			request_end = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(server_result.has_value());
	RIWO_TEST_CHECK_EQ(server_result->consumed, request_wire.size());
	RIWO_TEST_CHECK(server_stream != 0);
	RIWO_TEST_CHECK(request_end);

	http::response_head response;
	response.status = http::status::no_content;
	RIWO_TEST_CHECK(server.submit(http::send_response_action{
		.stream = server_stream, .head = response,
		.body = http::body_spec::empty()
	}).has_value());
	const auto response_wire = drain(server, 2);
	bool response_head = false;
	bool response_end = false;
	auto client_result = client.ingest(riwo::buffer(response_wire),
	[&](const http::protocol_event_t &event) {
		if( auto head = std::get_if<http::response_head_event>(&event) )
		{
			response_head = true;
			RIWO_TEST_CHECK_EQ(head->stream, 9U);
			RIWO_TEST_CHECK_EQ(head->head.status.code(), 204U);
		}
		if( std::holds_alternative<http::stream_end_event>(event) )
			response_end = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(client_result.has_value());
	RIWO_TEST_CHECK(response_head);
	RIWO_TEST_CHECK(response_end);
}

void h1_pause_resume_and_eof()
{
	http::v1::connection_engine client({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1
	});
	http::request_head request;
	request.method = http::method::get;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/paused";
	RIWO_TEST_CHECK(client.submit(http::open_request_action{
		.stream = 12, .head = request, .body = http::body_spec::empty()
	}).has_value());
	const auto wire = drain(client, 3);
	http::v1::connection_engine server({
		.role = http::protocol_role::server,
		.version = http::protocol::v1_1
	});
	size_t heads = 0;
	auto paused = server.ingest(riwo::buffer(wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::request_head_event>(event) ) ++heads;
		return http::event_flow::pause;
	});
	RIWO_TEST_CHECK(paused.has_value());
	RIWO_TEST_CHECK(paused->paused);
	RIWO_TEST_CHECK_EQ(paused->consumed, 0U);
	bool ended = false;
	auto resumed = server.ingest(riwo::buffer(wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::request_head_event>(event) ) ++heads;
		if( std::holds_alternative<http::stream_end_event>(event) ) ended = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(resumed.has_value());
	RIWO_TEST_CHECK_EQ(resumed->consumed, wire.size());
	RIWO_TEST_CHECK_EQ(heads, 2U);
	RIWO_TEST_CHECK(ended);

	http::v1::connection_engine truncated({
		.role = http::protocol_role::server,
		.version = http::protocol::v1_1
	});
	auto partial = truncated.ingest(riwo::buffer(std::string_view("GET / HTTP/1.1\r\n")),
		[](const http::protocol_event_t &) { return http::event_flow::continue_; });
	RIWO_TEST_CHECK(partial.has_value());
	RIWO_TEST_CHECK(partial->needs_input);
	auto eof = truncated.finish_input(
		[](const http::protocol_event_t &) { return http::event_flow::continue_; });
	RIWO_TEST_CHECK(!eof.has_value());
	RIWO_TEST_CHECK_EQ(eof.error(),
		http::make_error_code(http::errc::unexpected_eof));

	http::v1::connection_engine bounded({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1,
		.max_pending_output = 4
	});
	auto rejected = bounded.submit(http::open_request_action{
		.stream = 1, .head = std::move(request),
		.body = http::body_spec::empty()
	});
	RIWO_TEST_CHECK(!rejected.has_value());
	RIWO_TEST_CHECK_EQ(rejected.error(),
		http::make_error_code(http::errc::buffer_limit_exceeded));
	RIWO_TEST_CHECK(!bounded.wants_write());

	http::v1::connection_engine chunk_client({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1
	});
	http::request_head chunk_request;
	chunk_request.method = http::method::post;
	chunk_request.scheme = "http";
	chunk_request.authority = "example.test";
	chunk_request.target = "/trailers";
	RIWO_TEST_CHECK(chunk_client.submit(http::open_request_action{
		.stream = 19, .head = std::move(chunk_request),
		.body = http::body_spec::streaming({{"X-Checksum"}})
	}).has_value());
	RIWO_TEST_CHECK(chunk_client.submit(http::send_data_action{
		.stream = 19, .data = bytes("abc")
	}).has_value());
	http::fields outbound_trailers;
	outbound_trailers.add("X-Checksum", "ok");
	RIWO_TEST_CHECK(chunk_client.submit(http::send_trailers_action{
		.stream = 19, .trailers = std::move(outbound_trailers)
	}).has_value());
	const auto chunk_wire = drain(chunk_client, 5);
	http::v1::connection_engine chunk_server({
		.role = http::protocol_role::server,
		.version = http::protocol::v1_1
	});
	size_t consumed = 0;
	size_t trailer_events = 0;
	bool chunk_end = false;
	while( consumed < chunk_wire.size() )
	{
		auto result = chunk_server.ingest(riwo::buffer(
			std::string_view(chunk_wire).substr(consumed)),
		[&](const http::protocol_event_t &event) {
			if( std::holds_alternative<http::trailers_event>(event) )
			{
				++trailer_events;
				return http::event_flow::pause;
			}
			return http::event_flow::continue_;
		});
		RIWO_TEST_CHECK(result.has_value());
		consumed += result->consumed;
		if( result->paused ) break;
		RIWO_TEST_CHECK(result->consumed != 0);
	}
	RIWO_TEST_CHECK_EQ(trailer_events, 1U);
	auto trailer_resume = chunk_server.ingest(riwo::buffer(
		std::string_view(chunk_wire).substr(consumed)),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::trailers_event>(event) ) ++trailer_events;
		if( std::holds_alternative<http::stream_end_event>(event) ) chunk_end = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(trailer_resume.has_value());
	RIWO_TEST_CHECK_EQ(trailer_events, 2U);
	RIWO_TEST_CHECK(chunk_end);
}

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
static_assert(std::same_as<
	decltype(std::declval<const http::v2::connection_extension&>().local_settings()),
	http::v2::settings_snapshot>);
static_assert(std::same_as<
	decltype(std::declval<const http::v2::connection_extension&>().peer_settings()),
	http::v2::settings_snapshot>);
static_assert(std::same_as<
	decltype(std::declval<const http::v2::connection_extension&>().diagnostics()),
	http::v2::connection_diagnostics>);

void h2_manual_engine_loop()
{
	http::v2::connection_engine client({.role = http::protocol_role::client});
	http::v2::connection_engine server({.role = http::protocol_role::server});
	auto *diagnostics = client.extension<http::v2::connection_extension>();
	RIWO_TEST_CHECK(diagnostics != nullptr);
	RIWO_TEST_CHECK_EQ(diagnostics->local_settings().initial_window_size,
		http::v2::connection_config{}.initial_stream_window);
	RIWO_TEST_CHECK_EQ(diagnostics->pending_output(), 0U);
	RIWO_TEST_CHECK(client.extension<int>() == nullptr);
	http::request_head request;
	request.method = http::method::get;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/h2";
	RIWO_TEST_CHECK(client.submit(http::open_request_action{
		.stream = 77, .head = request, .body = http::body_spec::empty()
	}).has_value());

	http::stream_token_t server_stream = 0;
	bool response_received = false;
	auto server_events = [&](const http::protocol_event_t &event) {
		if( auto head = std::get_if<http::request_head_event>(&event) )
		{
			server_stream = head->stream;
			RIWO_TEST_CHECK_EQ(head->head.target, "/h2");
		}
		return http::event_flow::continue_;
	};
	auto client_events = [&](const http::protocol_event_t &event) {
		if( auto head = std::get_if<http::response_head_event>(&event) )
		{
			response_received = true;
			RIWO_TEST_CHECK_EQ(head->stream, 77U);
			RIWO_TEST_CHECK_EQ(head->head.status.code(), 204U);
		}
		return http::event_flow::continue_;
	};

	for(size_t round=0; round<16; ++round)
	{
		const auto to_server = drain(client, 3);
		if( not to_server.empty() )
		{
			auto result = server.ingest(riwo::buffer(to_server), server_events);
			RIWO_TEST_CHECK(result.has_value());
		}
		if( server_stream != 0 ) break;
		const auto to_client = drain(server, 3);
		if( not to_client.empty() )
		{
			auto result = client.ingest(riwo::buffer(to_client), client_events);
			RIWO_TEST_CHECK(result.has_value());
		}
	}
	RIWO_TEST_CHECK(server_stream != 0);
	http::response_head response;
	response.status = http::status::no_content;
	RIWO_TEST_CHECK(server.submit(http::send_response_action{
		.stream = server_stream, .head = response,
		.body = http::body_spec::empty()
	}).has_value());
	for(size_t round=0; round<16 and not response_received; ++round)
	{
		const auto to_client = drain(server, 2);
		if( not to_client.empty() )
		{
			auto result = client.ingest(riwo::buffer(to_client), client_events);
			RIWO_TEST_CHECK(result.has_value());
		}
		const auto to_server = drain(client, 2);
		if( not to_server.empty() )
		{
			auto result = server.ingest(riwo::buffer(to_server), server_events);
			RIWO_TEST_CHECK(result.has_value());
		}
	}
	RIWO_TEST_CHECK(response_received);
	RIWO_TEST_CHECK(client.wire_stream_id(77).has_value() or
		client.stream_from_wire_id(1).has_value() == false);
}

void h2_pause_resume()
{
	http::v2::connection_engine client({.role = http::protocol_role::client});
	http::v2::connection_engine server({.role = http::protocol_role::server});
	http::request_head request;
	request.method = http::method::get;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/paused";
	RIWO_TEST_CHECK(client.submit(http::open_request_action{
		.stream = 41, .head = std::move(request),
		.body = http::body_spec::empty()
	}).has_value());
	const auto wire = drain(client, 4096);
	size_t heads = 0;
	auto paused = server.ingest(riwo::buffer(wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::request_head_event>(event) )
		{
			++heads;
			return http::event_flow::pause;
		}
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(paused.has_value());
	RIWO_TEST_CHECK(paused->paused);
	RIWO_TEST_CHECK_EQ(paused->consumed, 0U);
	bool ended = false;
	auto resumed = server.ingest(riwo::buffer(wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::request_head_event>(event) ) ++heads;
		if( std::holds_alternative<http::stream_end_event>(event) ) ended = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(resumed.has_value());
	RIWO_TEST_CHECK_EQ(resumed->consumed, wire.size());
	RIWO_TEST_CHECK_EQ(heads, 2U);
	RIWO_TEST_CHECK(ended);

	http::v2::connection_engine body_client({.role = http::protocol_role::client});
	http::v2::connection_engine body_server({.role = http::protocol_role::server});
	http::request_head body_request;
	body_request.method = http::method::post;
	body_request.scheme = "http";
	body_request.authority = "example.test";
	body_request.target = "/body";
	RIWO_TEST_CHECK(body_client.submit(http::open_request_action{
		.stream = 42, .head = std::move(body_request),
		.body = http::body_spec::streaming()
	}).has_value());
	RIWO_TEST_CHECK(body_client.submit(http::send_data_action{
		.stream = 42, .data = bytes("abc"), .end_stream = true
	}).has_value());
	const auto body_wire = drain(body_client, 4096);
	size_t body_events = 0;
	auto body_pause = body_server.ingest(riwo::buffer(body_wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::body_data_event>(event) )
		{
			++body_events;
			return http::event_flow::pause;
		}
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(body_pause.has_value());
	RIWO_TEST_CHECK(body_pause->paused);
	RIWO_TEST_CHECK_EQ(body_pause->consumed, 0U);
	bool body_end = false;
	auto body_resume = body_server.ingest(riwo::buffer(body_wire),
	[&](const http::protocol_event_t &event) {
		if( std::holds_alternative<http::body_data_event>(event) ) ++body_events;
		if( std::holds_alternative<http::stream_end_event>(event) ) body_end = true;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(body_resume.has_value());
	RIWO_TEST_CHECK_EQ(body_resume->consumed, body_wire.size());
	RIWO_TEST_CHECK_EQ(body_events, 2U);
	RIWO_TEST_CHECK(body_end);

	http::v2::connection_engine eof_engine({.role = http::protocol_role::server});
	size_t eof_events = 0;
	auto eof_pause = eof_engine.finish_input(
	[&](const http::protocol_event_t &) {
		++eof_events;
		return http::event_flow::pause;
	});
	RIWO_TEST_CHECK(eof_pause.has_value());
	RIWO_TEST_CHECK(eof_pause->paused);
	auto eof_resume = eof_engine.finish_input(
	[&](const http::protocol_event_t &) {
		++eof_events;
		return http::event_flow::continue_;
	});
	RIWO_TEST_CHECK(eof_resume.has_value());
	RIWO_TEST_CHECK_EQ(eof_events, 2U);
}
#endif

void moved_from_protocol_objects_are_safe()
{
	http::v1::request_parser request_parser;
	auto moved_request_parser = std::move(request_parser);
	const std::string request_wire =
		"GET / HTTP/1.1\r\nHost: example.test\r\n\r\n";
	auto request = request_parser.ingest(riwo::buffer(request_wire));
	RIWO_TEST_CHECK(request.has_value());
	RIWO_TEST_CHECK(request->head.has_value());
	RIWO_TEST_CHECK(!moved_request_parser.complete());
	http::v1::request_parser assigned_request_parser;
	assigned_request_parser = std::move(moved_request_parser);
	RIWO_TEST_CHECK(!moved_request_parser.complete());

	http::v1::response_parser response_parser;
	auto moved_response_parser = std::move(response_parser);
	const std::string response_wire = "HTTP/1.1 204 No Content\r\n\r\n";
	auto response = response_parser.ingest(riwo::buffer(response_wire));
	RIWO_TEST_CHECK(response.has_value());
	RIWO_TEST_CHECK(response->head.has_value());
	RIWO_TEST_CHECK(!moved_response_parser.complete());

	http::v1::chunk_decoder chunks;
	auto moved_chunks = std::move(chunks);
	const std::string chunk_wire = "0\r\n\r\n";
	auto chunk = chunks.ingest(riwo::buffer(chunk_wire));
	RIWO_TEST_CHECK(chunk.has_value());
	RIWO_TEST_CHECK(chunk->message_end);
	RIWO_TEST_CHECK(!moved_chunks.complete());

	http::v1::connection_engine h1({
		.role = http::protocol_role::server,
		.version = http::protocol::v1_1
	});
	auto moved_h1 = std::move(h1);
	RIWO_TEST_CHECK(h1.selected_protocol() == http::protocol::v1_1);
	RIWO_TEST_CHECK(!h1.wants_write());
	RIWO_TEST_CHECK(moved_h1.selected_protocol() == http::protocol::v1_1);
	http::v1::connection_engine assigned_h1({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1
	});
	assigned_h1 = std::move(moved_h1);
	RIWO_TEST_CHECK(moved_h1.selected_protocol() == http::protocol::v1_1);
	RIWO_TEST_CHECK(assigned_h1.selected_protocol() == http::protocol::v1_1);

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	http::v2::connection_engine h2({.role = http::protocol_role::server});
	auto moved_h2 = std::move(h2);
	RIWO_TEST_CHECK(h2.selected_protocol() == http::protocol::v2);
	(void)h2.wants_write();
	RIWO_TEST_CHECK(moved_h2.selected_protocol() == http::protocol::v2);
	http::v2::connection_engine assigned_h2({
		.role = http::protocol_role::client});
	assigned_h2 = std::move(moved_h2);
	RIWO_TEST_CHECK(moved_h2.selected_protocol() == http::protocol::v2);
	RIWO_TEST_CHECK(assigned_h2.selected_protocol() == http::protocol::v2);
#endif
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"moved-from protocol objects are safe",
			moved_from_protocol_objects_are_safe},
		{"HTTP/1 codec fragmentation", h1_codec_fragmentation},
		{"HTTP/1 manual engine loop", h1_manual_engine_loop},
		{"HTTP/1 pause resume and EOF", h1_pause_resume_and_eof},
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
		{"HTTP/2 manual engine loop", h2_manual_engine_loop},
		{"HTTP/2 pause resume", h2_pause_resume}
#endif
	});
}
