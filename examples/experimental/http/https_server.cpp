// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::experimental::http;

int main(int argc, const char *argv[])
try
{
	if( argc < 3 )
	{
		std::cerr << "Usage: https_server <certificate.pem> <private-key.pem> "
			"[port] [auto|http1|http2]\n";
		return 2;
	}
	const auto port = static_cast<std::uint16_t>(
		argc > 3 ? std::stoul(argv[3]) : 8443
	);
	const auto requirement = http::example::parse_protocol (
		argc > 4 ? argv[4] : "auto"
	);
	http::server_tls_config tls_config;
	tls_config.certificate_chain_file = argv[1];
	tls_config.private_key_file = argv[2];
	tls_config.protocols.requirement = requirement;

	auto tls_context = std::make_shared
		<http::server_tls_context>(std::move(tls_config));

	http::server_config config;
	config.protocols.requirement = requirement;

	http::server service(std::move(tls_context), config);
	service
	.bind({riwo::ip_type::v4, port})

	.on_request<http::method::get>("/",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		http::response_head head;
		head.headers.add("Content-Type", "text/plain; charset=utf-8");

		co_await exchange.respond(std::move(head),
			"Hello over TLS\n", riwo::use_awaitable
		);
	})
	.on_server_error([](riwo::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on https://127.0.0.1:" << port << '\n';
	riwo::io_context().run();
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "EXP_HTTP HTTPS server failed: " << exception.what() << '\n';
	return 1;
}
