// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/retry.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const std::string endpoint = argc > 1 ?
		argv[1] : "ws://127.0.0.1:8080/echo";

	ws::client client;
	riwo::dispatch([&client, endpoint]() -> riwo::awaitable<void>
	{
		try {
			// Initial connection establishment is deliberately attempted once.
			auto stream = co_await client.open(
				ws::connect_request(endpoint), riwo::use_awaitable);

			ws::retry_open_options options;
			options.max_attempts = 8;

			options.observe = [](ws::retry_open_event event, const ws::retry_open_context &context)
			{
				if( event == ws::retry_open_event::waiting )
				{
					std::cerr << "WebSocket recovery attempt " << context.attempt
						<< " failed: " << context.error.message() << '\n';
				}
			};
			for(size_t generation = 1;; ++generation)
			{
				co_await stream.write_text (
					std::format("hello from connection {}", generation),
					riwo::use_awaitable
				);
				auto read_result = co_await stream
					.read<std::string>(asio::as_tuple(riwo::use_awaitable));

				auto &[error, message] = read_result;
				if( not error )
				{
					std::cout << '[' << generation << "] "
						<< message.body << '\n';
					continue;
				}
				// The application detected an invalid connection. This is where it
				// can pause work before explicitly asking the library to retry open.
				stream.shutdown();

				auto recovered = co_await ws::retry_open(client,
					[endpoint](const ws::retry_open_context&) {
						return ws::connect_request(endpoint);
					},
					options, riwo::use_awaitable
				);
				stream = std::move(recovered.stream);

				// Restore authentication, subscriptions, and cursors here before
				// allowing application work to continue.
			}
		}
		catch(const std::exception &exception)
		{
			std::cerr << "WebSocket client failed: " << exception.what() << '\n';
			riwo::exit(1);
			co_return ;
		}
	});
	return riwo::exec();
}
