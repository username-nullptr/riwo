// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader_t &reader)
{
	std::string result;
	std::array<char,1024> buffer {};
	for(;;)
	{
		auto read = co_await reader.read_some(
			riwo::buffer(buffer), riwo::use_awaitable);
		result.append(buffer.data(), read.bytes);
		if( read.eof ) break;
	}
	co_return result;
}

int run_server()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_t service(std::move(acceptor));
	bool succeeded = true;
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::post>("/interop/tool",
		[&](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			try
			{
				const auto &request = exchange.request();
				const auto marker = request.headers.find_first("X-Interop-Client");
				const auto body = co_await read_body(exchange.request_body());
				succeeded = marker && *marker == "external" &&
					request.target == "/interop/tool?value=42" &&
					body == "external-client";

				http::response_head head {
					succeeded ? http::status(uint16_t(202)) :
						http::status(uint16_t(400)), {}
				};
				head.headers.add("X-Riwo-Interop", "server");
				const auto response_body = succeeded ?
					std::string_view("riwo-server:external-client") :
					std::string_view("riwo-server:rejected");
				co_await exchange.respond(std::move(head), response_body,
					riwo::use_awaitable);
			}
			catch(const std::exception &error)
			{
				succeeded = false;
				std::cerr << "HTTP_EXP interoperability server failed: "
					<< error.what() << '\n';
			}
			service.stop();
		})
		.start();

	std::cout << "PORT " << service.acceptor().local_endpoint().port()
		<< std::endl;
	context.run();
	return succeeded ? 0 : 1;
}

int run_client(std::string_view endpoint)
{
	riwo::io_context_t context;
	http::client_t requester(context);
	http::request request(http::method::get, std::string(endpoint));
	request.headers().add("X-Interop-Client", "riwo");
	auto reply = requester.fetch(std::move(request));
	RIWO_TEST_CHECK_EQ(reply.status(), http::status(uint16_t(202)));
	RIWO_TEST_CHECK_EQ(reply.headers().find_first("X-Interop-Server").value(),
		"python-stdlib");
	RIWO_TEST_CHECK_EQ(std::string_view(
		reinterpret_cast<const char*>(reply.buffered_body().data()),
		reply.buffered_body().size()), "external-server:riwo-client");
	std::cout << "Riwo HTTP_EXP client interoperated with the external server\n";
	return 0;
}

} //namespace

int main(int argc, const char *argv[])
{
	try
	{
		if( argc == 2 && std::string_view(argv[1]) == "server" )
			return run_server();
		if( argc == 3 && std::string_view(argv[1]) == "client" )
			return run_client(argv[2]);
		std::cerr << "Usage: http_exp.interop-peer server | client <url>\n";
		return 2;
	}
	catch(const std::exception &error)
	{
		std::cerr << "HTTP_EXP interoperability peer failed: "
			<< error.what() << '\n';
		return 1;
	}
}
