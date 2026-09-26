// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/client.h>
#include <riwo/http/server.h>

namespace
{

int run_server()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor));
	bool succeeded = true;
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<method::post>("/interop/{name}",
		[&](server::context_t &request_context) -> riwo::awaitable<void>
		{
			try
			{
				auto &request = request_context.request();
				const auto marker = request.header("X-Interop-Client");
				const auto name = request.path_arg("name");
				const auto value = request.parameter("value");
				const auto body = co_await request.read<std::string>(
					riwo::use_awaitable);
				succeeded = marker and marker->to_string() == "external" and
					name and name->to_string() == "tool" and
					value and value->to_string() == "42" and
					body == "external-client";

				auto &response = request_context.response();
				response.set_status(succeeded ? status::accepted : status::bad_request);
				response.set_header("X-Riwo-Interop", "server");
				const auto response_body = succeeded ?
					std::string_view("riwo-server:external-client") :
					std::string_view("riwo-server:rejected");
				co_await response.write(asio::buffer(response_body),
					riwo::use_awaitable);
			}
			catch(const std::exception &error)
			{
				succeeded = false;
				std::cerr << "HTTP interoperability server failed: "
					<< error.what() << '\n';
			}
			service.stop();
		})
		.start();

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	std::cout << "PORT " << port << std::endl;
	context.run();
	return succeeded ? 0 : 1;
}

int run_client(std::string_view endpoint)
{
	using namespace riwo::http;
	request_arg arguments;
	arguments.set_header("X-Interop-Client", "riwo");
	client requester;
	auto request = requester.request_get(
		client::req_info(std::string(endpoint), std::move(arguments)));
	RIWO_TEST_CHECK(request);
	RIWO_TEST_CHECK_EQ(request->wait_reply(), status::accepted);
	const auto marker = request->reply()->header("X-Interop-Server");
	RIWO_TEST_CHECK(marker);
	RIWO_TEST_CHECK_EQ(marker->to_string(), "python-stdlib");
	RIWO_TEST_CHECK_EQ(request->reply()->read<std::string>(),
		"external-server:riwo-client");
	std::cout << "Riwo HTTP client interoperated with the external server\n";
	return 0;
}

} //namespace

int main(int argc, const char *argv[])
{
	try
	{
		if( argc == 2 and std::string_view(argv[1]) == "server" )
			return run_server();
		if( argc == 3 and std::string_view(argv[1]) == "client" )
			return run_client(argv[2]);
		std::cerr << "Usage: http.interop-peer server | client <url>\n";
		return 2;
	}
	catch(const std::exception &error)
	{
		std::cerr << "HTTP interoperability peer failed: " << error.what() << '\n';
		return 1;
	}
}
