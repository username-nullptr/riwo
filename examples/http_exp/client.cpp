// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] std::string as_string(std::span<const std::byte> bytes)
{
	return {
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size()
	};
}

} //namespace

int main(int argc, const char *argv[])
{
	try
	{
		const std::string base = argc > 1 ?
			argv[1] : "http://127.0.0.1:8084";
		riwo::io_context_t context;
		http::client_config config;
		config.protocols.requirement = http::protocol_requirement::require_http1;
		http::client client(context, config);

		auto health = client.get(base + "/health");
		auto health_body = health.read_all();
		std::cout << "GET /health -> " << health.status().code() << '\n'
			<< as_string(health_body);

		http::request echo_request(http::method::post, base + "/echo");
		echo_request.headers().add("Content-Type", "text/plain");
		echo_request.body("hello from http_exp\n");
		auto echoed = client.fetch(std::move(echo_request));
		std::cout << "POST /echo -> " << echoed.status().code() << '\n'
			<< as_string(echoed.buffered_body());

		auto streamed = client.send(
			http::request(http::method::get, base + "/stream"));
		auto streamed_body = streamed.read_all();
		auto trailers = streamed.body().trailers();
		std::cout << "GET /stream -> " << streamed.status().code() << '\n'
			<< as_string(streamed_body)
			<< "Trailer X-Chunk-Count: "
			<< trailers.find_first("X-Chunk-Count").value_or("missing") << '\n';
		return 0;
	}
	catch(const std::exception &exception)
	{
		std::cerr << "HTTP_EXP request failed: " << exception.what() << '\n';
		return 1;
	}
}
