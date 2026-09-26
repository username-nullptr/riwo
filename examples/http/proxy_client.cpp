// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client.h>
#include <iostream>
#include <string>

namespace http = riwo::http;

int main(int argc, const char *argv[])
{
	const std::string target = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080/hello/Proxy";

	const std::string proxy = argc > 2 ?
		argv[2] : "http://127.0.0.1:3128";

	http::client::req_info request(target);
	request.proxy = proxy;
	request.max_redirects = 3;

	if( argc > 3 )
		request.arg.set_proxy_basic_auth(argv[3], argc > 4 ? argv[4] : "");

	std::error_code error;
	http::client client;

	auto context = client.request_get(std::move(request), error);
	if( error )
	{
		std::cerr << "Proxy request failed: " << error.message() << '\n';
		return 1;
	}
	auto status = context->wait_reply(error);
	if( error )
	{
		std::cerr << "Proxy reply failed: " << error.message() << '\n';
		return 1;
	}
	auto body = context->reply()->read<std::string>(error);
	if( error )
	{
		std::cerr << "Proxy body read failed: " << error.message() << '\n';
		return 1;
	}
	std::cout << "HTTP status: " << static_cast<unsigned>(status) << '\n';
	std::cout << body;
	return 0;
}
