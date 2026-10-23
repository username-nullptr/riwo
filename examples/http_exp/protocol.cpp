// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/protocol.h>
#include <array>
#include <iostream>
#include <system_error>

namespace
{

namespace http = riwo::http_exp;

template <http::concepts::protocol_engine From, http::concepts::protocol_engine To, typename Handler>
size_t pump(From &from, To &to, Handler &&events)
{
	std::array<std::byte,128> buffer {};
	size_t transferred = 0;

	while( from.wants_write() )
	{
		auto produced = from.produce(riwo::buffer(buffer));
		if( not produced )
			riwo::system_error::loc_throw(produced.error());

		size_t offset = 0;
		while( offset != produced->produced )
		{
			auto input = riwo::const_buffer (
				buffer.data() + offset, produced->produced - offset
			);
			auto ingested = to.ingest(input, events);

			if( not ingested )
				riwo::system_error::loc_throw(ingested.error());

			if( ingested->paused or ingested->consumed == 0 )
				riwo::runtime_error::loc_throw("protocol engine paused unexpectedly");

			offset += ingested->consumed;
		}
		transferred += produced->produced;

		// The peer may need to ingest these bytes before more output is possible.
		if( produced->produced == 0 )
			break;
	}
	return transferred;
}

template <http::concepts::protocol_engine Client, http::concepts::protocol_engine Server>
void round_trip(std::string_view name, Client client, Server server)
{
	http::request_head request;
	request.method = http::method::get;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/hello";
	request.headers.add("Accept", "text/plain");

	auto submitted = client.submit(http::open_request_action {
		.stream = 1,
		.head = std::move(request),
		.body = http::body_spec::empty()
	});
	if( not submitted )
		riwo::system_error::loc_throw(submitted.error());

	http::stream_token_t server_stream = 0;
	auto request_bytes = pump(client, server, [&](const http::protocol_event_t &event)
	{
		if( const auto *head = std::get_if<http::request_head_event>(&event) )
			server_stream = head->stream;
		return http::event_flow::continue_;
	});
	// HTTP/2 may emit its initial SETTINGS before the request is delivered.
	for( size_t round = 0; server_stream == 0 and round != 4; ++round )
	{
		(void)pump(server, client, [](const auto &) {
			return http::event_flow::continue_;
		});
		request_bytes += pump(client, server, [&](const http::protocol_event_t &event)
		{
			if( const auto *head = std::get_if<http::request_head_event>(&event) )
				server_stream = head->stream;
			return http::event_flow::continue_;
		});
	}
	if( server_stream == 0 )
		riwo::runtime_error::loc_throw("server did not receive the request");

	http::response_head response;
	response.status = http::status::ok;
	response.headers.add("Content-Type", "text/plain");

	submitted = server.submit(http::send_response_action {
		.stream = server_stream,
		.head = std::move(response),
		.body = http::body_spec::empty()
	});
	if( not submitted )
		riwo::system_error::loc_throw(submitted.error());

	auto status = http::status::internal_server_error;
	size_t response_bytes = 0;

	for(size_t round=0; status!=http::status::ok and round!=4; ++round)
	{
		response_bytes += pump(server, client, [&](const http::protocol_event_t &event)
		{
			if( const auto *head = std::get_if<http::response_head_event>(&event) )
				status = head->head.status;
			return http::event_flow::continue_;
		});
		(void)pump(client, server, [](const auto &) {
			return http::event_flow::continue_;
		});
	}
	if( status != http::status::ok )
		riwo::runtime_error::loc_throw("client did not receive the response");

	std::cout << name << ": " << request_bytes << " request bytes, "
		<< response_bytes << " response bytes, status " << status
		<< '\n';
}

} //namespace

int main()
try
{
	round_trip("HTTP/1.1",
		http::v1::connection_engine ({
			.role = http::protocol_role::client,
			.version = http::protocol::v1_1
		}),
		http::v1::connection_engine ({
			.role = http::protocol_role::server,
			.version = http::protocol::v1_1
		})
	);
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	round_trip("HTTP/2",
		http::v2::connection_engine ({
			.role = http::protocol_role::client
		}),
		http::v2::connection_engine ({
			.role = http::protocol_role::server
		})
	);
#endif
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP protocol example failed: "
		<< exception.what() << '\n';
	return 1;
}
