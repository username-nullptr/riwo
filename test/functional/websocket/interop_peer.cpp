// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/websocket/client.h>
#include <riwo/websocket/server.h>

namespace
{

namespace ws = riwo::websocket;

int run_server()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	bool succeeded = true;
	ws::upgrade_options options;
	options.supported_subprotocols = {"interop.v1"};
	options.require_subprotocol = true;
	options.response_headers["X-Riwo-Interop"] = "server";
	service.on_connection("/interop",
		[&](ws::accept_result accepted) -> riwo::awaitable<void>
		{
			try
			{
				const auto marker = accepted.request.request_headers.find(
					"X-Interop-Client");
				const auto value = accepted.request.query_parameters.find("value");
				succeeded = marker != accepted.request.request_headers.end() and
					marker->second.to_string() == "external" and
					value != accepted.request.query_parameters.end() and
					value->second.to_string() == "42" and
					accepted.handshake.subprotocol == "interop.v1";
				auto message = co_await accepted.stream.read<std::string>(
					riwo::use_awaitable);
				succeeded = succeeded and message.type == ws::message_type::text and
					message.body == "external-client";
				co_await accepted.stream.write_text(succeeded ?
					"riwo-server:external-client" : "riwo-server:rejected",
					riwo::use_awaitable);
				const auto closed = co_await accepted.stream.close(
					ws::close_frame(ws::close_code::normal_closure, "riwo-server"),
					riwo::use_awaitable);
				succeeded = succeeded and closed.clean;
			}
			catch(const std::exception &error)
			{
				succeeded = false;
				std::cerr << "WebSocket interoperability server failed: "
					<< error.what() << '\n';
			}
			service.stop();
		}, options);
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	std::cout << "PORT " << port << std::endl;
	context.run();
	return succeeded ? 0 : 1;
}

int run_client(std::string endpoint)
{
	riwo::io_context_t context;
	ws::client requester(context.get_executor());
	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::connect_request request(std::move(endpoint));
			request.subprotocols = {"interop.v1"};
			request.request_options.set_header("X-Interop-Client", "riwo");
			ws::open_diagnostics diagnostics;
			auto stream = co_await requester.open(
				std::move(request), diagnostics, riwo::use_awaitable);
			RIWO_TEST_CHECK(diagnostics.reply);
			const auto marker = diagnostics.reply->header("X-Interop-Server");
			RIWO_TEST_CHECK(marker);
			RIWO_TEST_CHECK_EQ(marker->to_string(), "node-ws");
			RIWO_TEST_CHECK_EQ(stream.negotiated_subprotocol(), "interop.v1");
			co_await stream.write_text("riwo-client", riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.type, ws::message_type::text);
			RIWO_TEST_CHECK_EQ(response.body, "external-server:riwo-client");
			auto closed = co_await stream.close(
				ws::close_frame(ws::close_code::normal_closure, "riwo-client"),
				riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
		}, asio::use_future);
	context.run();
	completed.get();
	std::cout << "Riwo WebSocket client interoperated with the external server\n";
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
		std::cerr << "Usage: websocket.interop-peer server | client <url>\n";
		return 2;
	}
	catch(const std::exception &error)
	{
		std::cerr << "WebSocket interoperability peer failed: "
			<< error.what() << '\n';
		return 1;
	}
}
