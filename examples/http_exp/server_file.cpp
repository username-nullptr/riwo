// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::http_exp;

int main(int argc, const char *argv[])
try
{
	if( argc < 2 )
	{
		std::cerr << "Usage: server_file <download-file> [port] "
			"[upload-file] [auto|http1|http2]\n";
		return 2;
	}
	const auto download_file = std::filesystem::absolute(argv[1]);

	const auto port = static_cast<std::uint16_t>(
		argc > 2 ? std::stoul(argv[2]) : 8083
	);
	const auto upload_file = std::filesystem::absolute (
		argc > 3 ? argv[3] : "riwo-http-exp-uploaded.bin"
	);
	http::server_config config;
	config.protocols.requirement = http::example::parse_protocol (
		argc > 4 ? argv[4] : "auto"
	);
	config.resource_root = download_file.parent_path();

	http::server service(config);
	service
	.bind({riwo::ip_type::v4, port})

	.on_request<http::method::get>("/download",
	[download_name = download_file.filename()](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		co_await exchange.respond_file (
			download_name, riwo::use_awaitable
		);
	})
	.on_request<http::method::post>("/upload",
	[upload_file](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		const auto saved = co_await exchange.save_file (
			upload_file, riwo::use_awaitable
		);
		http::response_head head { http::status::created };
		head.headers.add("Content-Type", "text/plain; charset=utf-8");

		co_await exchange.respond(std::move(head),
			std::format("uploaded {} bytes\n", saved),
			riwo::use_awaitable
		);
	})
	.on_default (
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		co_await exchange.respond({ http::status::not_found },
			"not found\n", riwo::use_awaitable
		);
	})
	.on_server_error([](riwo::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n'
		<< "Download: " << download_file << " (ETag and Range enabled)\n"
		<< "Upload:   POST /upload -> " << upload_file << '\n';

	return riwo::exec();
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP file server failed: " << exception.what() << '\n';
	return 1;
}
