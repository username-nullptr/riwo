// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/server.h>
#include <string_view>
#include <iostream>

int main(int argc, const char *argv[])
{
	if(argc < 3)
	{
		std::cerr << "Usage: https_server <certificate.pem> <private-key.pem> [port]\n";
		return 2;
	}
	const auto port = static_cast<std::uint16_t>(
		argc > 3 ? std::stoul(argv[3]) : 8443
	);
	try {
		asio::ssl::context tls(asio::ssl::context::tls_server);
		tls.set_options (
			asio::ssl::context::default_workarounds |
			asio::ssl::context::no_sslv2 |
			asio::ssl::context::no_sslv3
		);
		tls.use_certificate_chain_file(argv[1]);
		tls.use_private_key_file(argv[2], asio::ssl::context::pem);

		asio::ip::tcp::acceptor acceptor(riwo::get_executor());
		riwo::https::server server({std::move(acceptor), tls});

		server
		.bind({riwo::ip_type::v4, port})
		.on_request<riwo::http::method::get>(
			"/",
			[](riwo::https::server::context_t &context) -> riwo::awaitable<void>
			{
				constexpr std::string_view body = "Hello over TLS\n";
				co_await context.response().write (
					asio::buffer(body), riwo::use_awaitable
				);
				co_return;
			}
		)
		.start();

		std::cout << "Listening on https://127.0.0.1:" << port << '\n';
		return riwo::exec();
	}
	catch(const std::exception &exception) {
		std::cerr << "HTTPS server failed: " << exception.what() << '\n';
	}
	return 1;
}
