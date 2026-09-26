// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/client.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const std::string endpoint = argc > 1 ?
		argv[1] : "wss://127.0.0.1:8443/echo";

	try {
		asio::ssl::context tls(asio::ssl::context::tls_client);
		tls.set_options (
			asio::ssl::context::default_workarounds |
			asio::ssl::context::no_tlsv1 |
			asio::ssl::context::no_tlsv1_1 |
			asio::ssl::context::no_sslv2 |
			asio::ssl::context::no_sslv3
		);
		tls.set_verify_mode(asio::ssl::verify_peer);
		tls.set_default_verify_paths();

		if( argc > 2 )
			tls.load_verify_file(argv[2]);

		auto connector = std::make_shared
			<riwo::http::connector>(riwo::get_executor(), tls);

		riwo::http::connection_pool pool(std::move(connector));
		riwo::http::client http_client(std::move(pool));
		ws::client client(std::move(http_client));

		riwo::dispatch([&client, endpoint]() -> riwo::awaitable<void>
		{
			try {
				auto stream = co_await client
					.open(ws::connect_request(endpoint), riwo::use_awaitable);

				co_await stream.write_text("hello", riwo::use_awaitable);
				auto message = co_await stream.read<std::string>(riwo::use_awaitable);

				std::cout << message.body << '\n';
				co_await stream.close(riwo::use_awaitable);
			}
			catch(const std::exception &exception)
			{
				std::cerr << "Secure WebSocket client failed: "
					<< exception.what() << '\n';

				riwo::exit(1);
				co_return ;
			}
			riwo::exit();
			co_return ;
		});
		return riwo::exec();
	}
	catch(const std::exception &exception) {
		std::cerr << "WSS client setup failed: " << exception.what() << '\n';
	}
	return 1;
}
