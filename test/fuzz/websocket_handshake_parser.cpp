// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/protocol/handshake.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size == 0 )
		return 0;

	const std::string value(reinterpret_cast<const char*>(data + 1), size - 1);
	riwo::http::headers headers {
		{riwo::http::header::connection, "Upgrade"},
		{riwo::http::header::upgrade, "websocket"},
		{"Sec-WebSocket-Version", "13"},
		{"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
	};
	switch(data[0] % 6)
	{
	case 0: headers[riwo::http::header::connection] = value; break;
	case 1: headers[riwo::http::header::upgrade] = value; break;
	case 2: headers["Sec-WebSocket-Version"] = value; break;
	case 3: headers["Sec-WebSocket-Key"] = value; break;
	case 4: headers["Sec-WebSocket-Protocol"] = value; break;
	default: headers["Sec-WebSocket-Extensions"] = value; break;
	}

	auto request = riwo::websocket::parse_opening_request(
		riwo::http::method::get, riwo::http::version::v11, headers);
	if( not request )
		return 0;

	auto generated = riwo::websocket::make_opening_request_headers(*request);
	if( generated )
		riwo::ignore_unused(riwo::websocket::parse_opening_request(
			riwo::http::method::get, riwo::http::version::v11, *generated));

	riwo::websocket::opening_response response;
	if( not request->subprotocols.empty() )
		response.subprotocol = request->subprotocols.front();
	if( not request->extensions.empty() )
		response.extensions.push_back(request->extensions.front());
	const auto response_headers =
		riwo::websocket::make_opening_response_headers(*request, response);
	if(response_headers)
	{
		const auto parsed_response = riwo::websocket::parse_opening_response(
			riwo::http::status::switching_protocols, *response_headers, *request);
		if(not parsed_response or
			parsed_response->subprotocol != response.subprotocol or
			parsed_response->extensions.size() != response.extensions.size())
			std::abort();
	}
	return 0;
}
