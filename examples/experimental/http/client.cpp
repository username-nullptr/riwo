// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::experimental::http;

int main(int argc, const char *argv[])
try
{
	const std::string base = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080";

	http::client client;
	auto config = client.config();

	config.protocols.requirement = http::example::parse_protocol (
		argc > 2 ? argv[2] : "auto"
	);
	client.set_config(std::move(config));

	http::request echo_request(http::method::post, base + "/echo");
	echo_request.headers()
		.add("Content-Type", "text/plain")
		.add("X-Example", "buffered-request");

	echo_request.set_body("hello from exp_http\n");
	auto echoed = client.fetch(std::move(echo_request));

	std::cout << "POST /echo ["
		<< http::example::protocol_text(echoed.selected_protocol())
		<< "] -> " << echoed.status().code() << '\n'
		<< http::example::body_text(echoed.buffered_body());

	auto streamed = client.send ({
		http::method::get, base + "/stream"
	});
	auto streamed_body = streamed.read_all();
	auto trailers = streamed.body().trailers();

	std::cout << "GET /stream ["
		<< http::example::protocol_text(streamed.selected_protocol())
		<< "] -> " << streamed.status().code() << '\n'
		<< http::example::body_text(streamed_body)
		<< "Trailer X-Chunk-Count: "
		<< trailers.find_first("X-Chunk-Count").value_or("missing") << '\n';

#if RIWO_EXP_HTTP_ZLIB_SUPPORT
	auto large = client.fetch ({
		http::method::get, base + "/large"
	});
	std::cout << "GET /large ["
		<< http::example::protocol_text(large.selected_protocol())
		<< "] -> " << large.status().code() << '\n'
		<< "Automatically decoded body bytes: "
		<< large.buffered_body().size() << '\n';
#endif //RIWO_EXP_HTTP_ZLIB_SUPPORT
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "EXP_HTTP request failed: " << exception.what() << '\n';
	return 1;
}
