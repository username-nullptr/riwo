// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::http_exp;

int main(int argc, const char *argv[])
try
{
	const std::string target = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080/hello/Proxy";

	const std::string proxy_url = argc > 2 ?
		argv[2] : "http://127.0.0.1:3128";

	http::client client;
	http::proxy_policy proxy;
	proxy.forward_proxy = riwo::url(proxy_url);

	if( argc > 3 )
	{
		proxy.username = argv[3];
		proxy.password = argc > 4 ? argv[4] : "";
	}
	http::request request(http::method::get, target);
	request.set_proxy(std::move(proxy));

	http::redirect_policy redirects;
	redirects.max_redirects = 3;

	request.set_redirects(redirects);
	auto reply = client.fetch(std::move(request));

	std::cout << "HTTP status: " << reply.status().code() << '\n';
	std::cout << http::example::body_text(reply.buffered_body());
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "Proxy request failed: " << exception.what() << '\n';
	return 1;
}
