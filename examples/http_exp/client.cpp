// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] http::protocol_requirement protocol_requirement(
	std::string_view value)
{
	if( value == "auto" ) return http::protocol_requirement::automatic;
	if( value == "http1" ) return http::protocol_requirement::require_http1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" ) return http::protocol_requirement::require_http2;
#endif
	throw std::invalid_argument(
		"protocol must be auto, http1"
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		" or http2"
#endif
	);
}

[[nodiscard]] std::string_view protocol_name(http::protocol value) noexcept
{
	switch(value)
	{
	case http::protocol::v1_0: return "HTTP/1.0";
	case http::protocol::v1_1: return "HTTP/1.1";
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	case http::protocol::v2: return "HTTP/2";
#endif
	}
	return "unknown";
}

[[nodiscard]] std::string as_string(std::span<const std::byte> bytes)
{
	return {
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size()
	};
}

} //namespace

int main(int argc, const char *argv[])
try
{
	const std::string base = argc > 1 ?
		argv[1] : "http://127.0.0.1:8084";
	const auto requirement = protocol_requirement(
		argc > 2 ? std::string_view(argv[2]) : std::string_view("auto"));

	riwo::io_context_t context;
	http::client_config config;
	config.protocols.requirement = requirement;
	config.pool.idle_timeout = std::chrono::milliseconds(1);
	config.timeout.body_idle = std::chrono::milliseconds(500);
	http::client client(context, config);

	auto health = client.get(base + "/health");
	auto health_body = health.read_all();
	std::cout << "GET /health [" << protocol_name(health.selected_protocol())
		<< "] -> " << health.status().code() << '\n'
		<< as_string(health_body);

	auto greeting = client.fetch(http::request(http::method::get,
		base + "/hello/Ada?tag=http&tag=streaming"));
	std::cout << "GET /hello/Ada [" << protocol_name(
		greeting.selected_protocol()) << "] -> "
		<< greeting.status().code() << '\n'
		<< as_string(greeting.buffered_body());

	http::request echo_request(http::method::post, base + "/echo");
	echo_request.headers().add("Content-Type", "text/plain");
	echo_request.body("hello from http_exp\n");
	auto echoed = client.fetch(std::move(echo_request));
	std::cout << "POST /echo [" << protocol_name(echoed.selected_protocol())
		<< "] -> " << echoed.status().code() << '\n'
		<< as_string(echoed.buffered_body());

	auto first_visit = client.fetch(
		http::request(http::method::get, base + "/visit"));
	auto second_visit = client.fetch(
		http::request(http::method::get, base + "/visit"));
	std::cout << "Session cookie persisted across requests: "
		<< first_visit.status().code() << ' ' << second_visit.status().code()
		<< '\n' << as_string(first_visit.buffered_body())
		<< as_string(second_visit.buffered_body());

	auto streamed = client.send(
		http::request(http::method::get, base + "/stream"));
	auto streamed_body = streamed.read_all();
	auto trailers = streamed.body().trailers();
	std::cout << "GET /stream [" << protocol_name(streamed.selected_protocol())
		<< "] -> " << streamed.status().code() << '\n'
		<< as_string(streamed_body)
		<< "Trailer X-Chunk-Count: "
		<< trailers.find_first("X-Chunk-Count").value_or("missing") << '\n';

	auto redirected = client.fetch(
		http::request(http::method::get, base + "/login"));
	std::cout << "GET /login -> redirected to /whoami ["
		<< protocol_name(redirected.selected_protocol()) << "] -> "
		<< redirected.status().code() << '\n'
		<< as_string(redirected.buffered_body())
		<< "Stored cookies: " << client.cookie_store()->size() << '\n';

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
	auto large = client.fetch(
		http::request(http::method::get, base + "/large"));
	std::cout << "GET /large [" << protocol_name(large.selected_protocol())
		<< "] -> " << large.status().code() << '\n'
		<< "Automatically decoded body bytes: "
		<< large.buffered_body().size() << '\n';
#endif
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP request failed: " << exception.what() << '\n';
	return 1;
}
