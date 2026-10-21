// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/protocol/v1/engine.h>
#include <iostream>

int main()
{
	namespace http = riwo::http_exp;
	http::v1::connection_engine engine({
		.role = http::protocol_role::client,
		.version = http::protocol::v1_1
	});
	http::request_head head;
	head.method = http::method::get;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/manual";
	auto submitted = engine.submit(http::open_request_action{
		.stream = 1,
		.head = std::move(head),
		.body = http::body_spec::empty()
	});
	if( not submitted )
	{
		std::cerr << submitted.error().message() << '\n';
		return 1;
	}
	std::array<char,32> output {};
	while( engine.wants_write() )
	{
		auto produced = engine.produce(riwo::buffer(output));
		if( not produced )
		{
			std::cerr << produced.error().message() << '\n';
			return 1;
		}
		std::cout.write(output.data(),
			static_cast<std::streamsize>(produced->produced));
	}
	return 0;
}
