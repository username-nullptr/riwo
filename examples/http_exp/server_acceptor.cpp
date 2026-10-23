// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

using executor_t = riwo::io_context_t::executor_type;
using acceptor_wrap_t = http::basic_acceptor_wrap<executor_t>;

class tracing_acceptor
{
public:
	using executor_type = executor_t;

	using acceptor_t = acceptor_wrap_t::acceptor_t;
	using socket_t = acceptor_wrap_t::socket_t;

	using accepted_t = acceptor_wrap_t::accepted_t;
	using accept_handler_t = acceptor_wrap_t::accept_handler_t;

	explicit tracing_acceptor(riwo::io_context_t &context) :
		m_acceptor(context) {}

	[[nodiscard]] executor_t get_executor() noexcept {
		return m_acceptor.get_executor();
	}

	void accept(accept_handler_t handler)
	{
		auto socket = std::make_shared<socket_t>(m_acceptor.get_executor());

		m_acceptor.async_accept(*socket,
		[socket, handler = std::move(handler)](riwo::error_code error) mutable
		{
			if( error )
			{
				std::move(handler)(error, accepted_t{});
				return;
			}
			std::cout << "Accepted " << socket->remote_endpoint() << '\n';

			std::move(handler)(riwo::error_code{}, accepted_t (
				std::in_place_type<socket_t>, std::move(*socket)
			));
		});
	}

	void cancel() noexcept
	{
		riwo::error_code ignored;
		m_acceptor.cancel(ignored);
		m_acceptor.close(ignored);
	}

	[[nodiscard]] const acceptor_t &acceptor() const noexcept {
		return m_acceptor;
	}
	[[nodiscard]] acceptor_t &acceptor() noexcept {
		return m_acceptor;
	}

private:
	acceptor_t m_acceptor;
};

} //namespace

int main(int argc, const char *argv[])
try
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8086
	);
	riwo::io_context_t context;
	auto acceptor = acceptor_wrap_t(tracing_acceptor(context));

	http::basic_server service(std::move(acceptor));
	service
	.bind({riwo::ip_type::v4, port})

	.on_request<http::method::get>("/",
	[](http::basic_server_exchange<executor_t> &exchange) -> riwo::awaitable<void>
	{
		co_await exchange.respond (
			"Hello from a custom acceptor\n", riwo::use_awaitable
		);
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n';
	context.run();
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "Custom acceptor failed: " << exception.what() << '\n';
	return 1;
}
