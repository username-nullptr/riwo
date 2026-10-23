// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/protocol/v1/engine.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] std::vector<std::byte> bytes(std::string_view value)
{
	std::vector<std::byte> result(value.size());
	std::memcpy(result.data(), value.data(), value.size());
	return result;
}

[[nodiscard]] std::string drain(http::concepts::protocol_engine auto &engine,
	size_t chunk_size = 7)
{
	std::string result;
	std::vector<std::byte> buffer(chunk_size);
	while( engine.wants_write() )
	{
		auto produced = engine.produce(riwo::buffer(buffer));
		if( not produced )
			throw std::runtime_error(produced.error().message());
		result.append(reinterpret_cast<const char*>(buffer.data()),
			produced->produced);
		if( produced->produced == 0 and not produced->has_more )
			break;
	}
	return result;
}

void ingest_or_throw(http::concepts::protocol_engine auto &engine,
	std::string_view wire, const http::protocol_event_handler_t &handler)
{
	size_t offset = 0;
	while( offset != wire.size() )
	{
		auto result = engine.ingest(riwo::buffer(wire.substr(offset)), handler);
		if( not result )
			throw std::runtime_error(result.error().message());
		if( result->consumed == 0 )
			throw std::runtime_error(
				"protocol engine stopped before consuming the whole input");
		offset += result->consumed;
	}
}

} //namespace

int main()
try
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
	request.method = http::method::post;
	request.scheme = "http";
	request.authority = "example.test";
	request.target = "/manual?name=Riwo";
	request.headers.add("Content-Type", "text/plain");
	if( not client.submit(http::open_request_action{
		.stream = 1,
		.head = std::move(request),
		.body = http::body_spec::streaming({{"X-Checksum"}})
	}) )
		throw std::runtime_error("client rejected the request head");
	if( not client.submit(http::send_data_action{
		.stream = 1, .data = bytes("manual engine body")
	}) )
		throw std::runtime_error("client rejected request data");
	http::fields request_trailers;
	request_trailers.add("X-Checksum", "ok");
	if( not client.submit(http::send_trailers_action{
		.stream = 1, .trailers = std::move(request_trailers)
	}) )
		throw std::runtime_error("client rejected request trailers");

	const auto request_wire = drain(client);
	std::cout << "Generated request (" << request_wire.size() << " bytes):\n"
		<< request_wire;

	http::stream_token_t server_stream = 0;
	std::string server_body;
	std::string server_trailer;
	bool request_complete = false;
	ingest_or_throw(server, request_wire,
		[&](const http::protocol_event_t &event)
		{
			if( const auto *head =
				std::get_if<http::request_head_event>(&event) )
			{
				server_stream = head->stream;
				std::cout << "Server event: request "
					<< head->head.method.text() << ' ' << head->head.target
					<< '\n';
			}
			else if( const auto *data =
				std::get_if<http::body_data_event>(&event) )
				server_body.append(static_cast<const char*>(data->data.data()),
					data->data.size());
			else if( const auto *trailers =
				std::get_if<http::trailers_event>(&event) )
				server_trailer = trailers->trailers.find_first(
					"X-Checksum").value_or("missing");
			else if( std::holds_alternative<http::stream_end_event>(event) )
				request_complete = true;
			return http::event_flow::continue_;
		});
	if( server_stream == 0 or not request_complete or server_body.empty() )
		throw std::runtime_error("server did not receive a complete request");
	std::cout << "Server decoded body: " << server_body << '\n'
		<< "Server decoded trailer X-Checksum: " << server_trailer << '\n';

	http::response_head response;
	response.status = http::status::ok;
	response.headers.add("Content-Type", "text/plain");
	if( not server.submit(http::send_response_action{
		.stream = server_stream,
		.head = std::move(response),
		.body = http::body_spec::streaming({{"X-Protocol"}})
	}) )
		throw std::runtime_error("server rejected the response head");
	if( not server.submit(http::send_data_action{
		.stream = server_stream, .data = bytes("response from ")
	}) )
		throw std::runtime_error("server rejected response data");
	http::fields response_trailers;
	response_trailers.add("X-Protocol", "http_exp");
	if( not server.submit(http::send_trailers_action{
		.stream = server_stream, .trailers = std::move(response_trailers)
	}) )
		throw std::runtime_error("server rejected response trailers");

	const auto response_wire = drain(server);
	std::cout << "\nGenerated response (" << response_wire.size()
		<< " bytes):\n" << response_wire;

	std::string client_body;
	std::string client_trailer;
	http::status response_status = http::status::internal_server_error;
	bool response_complete = false;
	ingest_or_throw(client, response_wire,
		[&](const http::protocol_event_t &event)
		{
			if( const auto *head =
				std::get_if<http::response_head_event>(&event) )
				response_status = head->head.status;
			else if( const auto *data =
				std::get_if<http::body_data_event>(&event) )
				client_body.append(static_cast<const char*>(data->data.data()),
					data->data.size());
			else if( const auto *trailers =
				std::get_if<http::trailers_event>(&event) )
				client_trailer = trailers->trailers.find_first(
					"X-Protocol").value_or("missing");
			else if( std::holds_alternative<http::stream_end_event>(event) )
				response_complete = true;
			return http::event_flow::continue_;
		});
	if( response_status != http::status::ok or not response_complete )
		throw std::runtime_error("client did not receive a complete response");
	std::cout << "\nClient decoded status: " << response_status.code() << '\n'
		<< "Client decoded body: " << client_body << '\n'
		<< "Client decoded trailer X-Protocol: " << client_trailer << '\n';
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP protocol example failed: "
		<< exception.what() << '\n';
	return 1;
}
