// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/client.h>
#include <iostream>
#include <string>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const std::string endpoint = argc > 1 ?
		argv[1] : "ws://127.0.0.1:8080/echo";

	const std::string proxy_url = argc > 2 ?
		argv[2] : "http://127.0.0.1:3128";

	ws::proxy_config proxy {
		.endpoint = proxy_url,
	};
	if( proxy.endpoint.protocol() == "socks5" or
		proxy.endpoint.protocol() == "socks5h" )
		proxy.type = ws::proxy_type::socks5;

	if( argc > 3 )
		proxy.set_basic_auth(argv[3], argc > 4 ? argv[4] : "");

	ws::connect_request request(endpoint);
	request.proxy = std::move(proxy);

	std::error_code error;
	ws::client client;

	auto stream = client.open(std::move(request), error);
	if( error )
	{
		std::cerr << "Proxy connection failed: " << error.message() << '\n';
		return 1;
	}
	stream.write_text("hello through proxy", error);
	if( error )
	{
		std::cerr << "WebSocket write failed: " << error.message() << '\n';
		return 1;
	}
	auto message = stream.read<std::string>(error);
	if( error )
	{
		std::cerr << "WebSocket read failed: " << error.message() << '\n';
		return 1;
	}
	std::cout << message.body << '\n';
	riwo::ignore_unused(stream.close(error));
	if( error )
	{
		std::cerr << "WebSocket close failed: " << error.message() << '\n';
		return 1;
	}
	return 0;
}
