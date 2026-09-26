// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/server.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	if( argc < 3 )
	{
		std::cerr << "Usage: wss_server <certificate.pem> <private-key.pem> [port]\n";
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
		ws::tls_server server({std::move(acceptor), tls});
		server
		.bind({riwo::ip_type::v4, port})

		.on_connection("/echo",
		[](ws::tls_server::accept_result_t accepted) -> riwo::awaitable<void>
		{
			try {
				auto message = co_await accepted
					.stream.read<std::string>(riwo::use_awaitable);

				co_await accepted.stream
					.write_text("secure echo: " + message.body, riwo::use_awaitable);

				co_await accepted.stream.close(riwo::use_awaitable);
			}
			catch(const std::exception &exception)
			{
				std::cerr << "Secure WebSocket session failed: "
					<< exception.what() << '\n';
			}
			co_return ;
		})
		.on_server_error([](riwo::error_code error)
		{
			std::cerr << "Secure WebSocket server failed: "
				<< error.message() << '\n';
			return true;
		})
		.start();

		std::cout << "Listening on wss://127.0.0.1:" << port << "/echo\n";
		return riwo::exec();
	}
	catch(const std::exception &exception) {
		std::cerr << "WSS server failed: " << exception.what() << '\n';
	}
	return 1;
}
