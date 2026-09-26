// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/server.h>
#include <iostream>
#include <memory>

class request_log final : public riwo::http::server::aop_t
{
public:
	riwo::awaitable<bool> before(context_t &context) override
	{
		std::cout << "before: " << context.request().path() << '\n';
		co_return false;
	}

	riwo::awaitable<bool> after(context_t &context) override
	{
		std::cout << "after: " << context.request().path() << '\n';
		co_return false;
	}
};

class greeting_controller final : public riwo::http::server::ctrlr_aop_t
{
public:
	riwo::awaitable<void> service(context_t &context) override
	{
		constexpr std::string_view body = "Hello from a controller\n";
		co_await context.response().write(
			asio::buffer(body), riwo::use_awaitable
		);
		co_return;
	}
};

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8081
	);
	asio::ip::tcp::acceptor acceptor(riwo::get_executor());
	riwo::http::server server(std::move(acceptor));

	server
	.bind({riwo::ip_type::v4, port})
	.on_request<riwo::http::method::get>(
		"/hello",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			constexpr std::string_view body = "Hello through middleware\n";
			co_await context.response().write (
				asio::buffer(body), riwo::use_awaitable
			);
			co_return;
		},
		std::make_shared<request_log>()
	)
	.on_request<riwo::http::method::get>(
		"/controller", std::make_shared<greeting_controller>()
	)
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n';
	return riwo::exec();
}
