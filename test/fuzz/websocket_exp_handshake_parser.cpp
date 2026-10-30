// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp/protocol/handshake.h>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size == 0 )
		return 0;

	const std::string value(reinterpret_cast<const char*>(data + 1), size - 1);
	riwo::http_exp::fields headers;
	headers.add("Connection", "Upgrade");
	headers.add("Upgrade", "websocket");
	headers.add("Sec-WebSocket-Version", "13");
	headers.add("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
	try {
		switch(data[0] % 6)
		{
		case 0: headers.set("Connection", value); break;
		case 1: headers.set("Upgrade", value); break;
		case 2: headers.set("Sec-WebSocket-Version", value); break;
		case 3: headers.set("Sec-WebSocket-Key", value); break;
		case 4: headers.set("Sec-WebSocket-Protocol", value); break;
		default: headers.set("Sec-WebSocket-Extensions", value); break;
		}
	}
	catch(const std::invalid_argument&) {
		return 0;
	}

	auto request = riwo::websocket_exp::parse_opening_request(
		riwo::http_exp::method::get, riwo::http_exp::protocol::v1_1, headers);
	if( not request )
		return 0;

	auto generated =
		riwo::websocket_exp::make_opening_request_headers(*request);
	if( generated )
	{
		riwo::ignore_unused(riwo::websocket_exp::parse_opening_request(
			riwo::http_exp::method::get, riwo::http_exp::protocol::v1_1,
			*generated));
	}

	riwo::websocket_exp::opening_response response;
	if( not request->subprotocols.empty() )
		response.subprotocol = request->subprotocols.front();
	if( not request->extensions.empty() )
		response.extensions.push_back(request->extensions.front());

	const auto response_headers =
		riwo::websocket_exp::make_opening_response_headers(*request, response);
	if( response_headers )
	{
		const auto parsed_response =
			riwo::websocket_exp::parse_opening_response(
				riwo::http_exp::status::switching_protocols,
				*response_headers, *request);
		if( not parsed_response or
			parsed_response->subprotocol != response.subprotocol or
			parsed_response->extensions.size() != response.extensions.size() )
		{
			std::abort();
		}
	}
	return 0;
}

