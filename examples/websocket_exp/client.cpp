// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp.h>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;
namespace ws = riwo::websocket_exp;

[[nodiscard]] http::protocol_requirement protocol_requirement(
	std::string_view value)
{
	if( value == "http1" ) return http::protocol_requirement::require_v1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" ) return http::protocol_requirement::require_v2;
#endif
	throw std::invalid_argument(
		"protocol must be http1"
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		" or http2"
#endif
	);
}

[[nodiscard]] std::string_view protocol_name(http::protocol value) noexcept
{
	switch(value)
	{
	case http::protocol::v1_0: return "HTTP/1.0 Upgrade";
	case http::protocol::v1_1: return "HTTP/1.1 Upgrade";
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	case http::protocol::v2: return "HTTP/2 Extended CONNECT";
#endif
	}
	return "unknown";
}

} //namespace

int main(int argc, const char *argv[])
try
{
	const std::string endpoint = argc > 1 ?
		argv[1] : "ws://127.0.0.1:8085/echo";
	const std::string_view policy = argc > 2 ? argv[2] : "http1";

	riwo::io_context_t context;
	ws::client_config config;
	config.http.protocols.requirement = protocol_requirement(policy);
	ws::client client(context, config);

	ws::connect_request request;
	request.endpoint = endpoint;
	request.subprotocols = {"riwo.echo.v1"};
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
	request.extensions.push_back(ws::permessage_deflate_extension());
#endif
	ws::open_diagnostics diagnostics;
	auto stream = client.open(std::move(request), diagnostics);
	stream.write_text("hello from websocket_exp");
	auto response = stream.read<std::string>();

	std::cout << "Connected with "
		<< protocol_name(diagnostics.selected_protocol.value()) << '\n'
		<< "Subprotocol: " << stream.negotiated_subprotocol() << '\n'
		<< "Echo: " << response.body << '\n';
	const auto closed = stream.close(
		ws::close_frame(ws::close_code::normal_closure, "example complete"));
	std::cout << "Clean close: " << std::boolalpha << closed.clean << '\n';
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "WebSocket_EXP client failed: " << exception.what() << '\n';
	return 1;
}
