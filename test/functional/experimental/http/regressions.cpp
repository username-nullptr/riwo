// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/experimental/http.h>
#include <riwo/core/cxx/asio.h>

#include <array>
#include <string>
#include <thread>

namespace
{

namespace http = riwo::experimental::http;

class raw_client
{
public:
	raw_client(riwo::io_context_t &context, uint16_t port) :
		socket_(context)
	{
		riwo::error_code error;
		socket_.connect({asio::ip::make_address("127.0.0.1"), port}, error);
		if( error ) throw std::system_error(error);
	}

	void send(std::string_view wire)
	{
		riwo::error_code error;
		(void)asio::write(socket_, asio::buffer(wire), error);
		if( error ) throw std::system_error(error);
	}

	[[nodiscard]] std::string read_head()
	{
		riwo::error_code error;
		const auto size = asio::read_until(socket_, asio::dynamic_buffer(pending_),
			"\r\n\r\n", error);
		if( error ) throw std::system_error(error);
		std::string head = pending_.substr(0, size);
		pending_.erase(0, size);
		return head;
	}

	[[nodiscard]] std::string read_body(std::size_t size)
	{
		riwo::error_code error;
		while( pending_.size() < size )
		{
			std::array<char,4096> buffer {};
			const auto read = socket_.read_some(asio::buffer(buffer), error);
			if( error ) throw std::system_error(error);
			pending_.append(buffer.data(), read);
		}
		std::string body = pending_.substr(0, size);
		pending_.erase(0, size);
		return body;
	}

	[[nodiscard]] std::size_t buffered() const noexcept {
		return pending_.size();
	}

private:
	asio::ip::tcp::socket socket_;
	std::string pending_;
};

[[nodiscard]] int status_of(std::string_view head)
{
	const auto first = head.find(' ');
	const auto second = first == std::string_view::npos ? std::string_view::npos :
		head.find(' ', first + 1);
	if( first == std::string_view::npos || second == std::string_view::npos )
		return 0;
	return std::stoi(std::string(head.substr(first + 1, second - first - 1)));
}

[[nodiscard]] bool has_header(std::string_view head, std::string_view name)
{
	auto lower = riwo::strtls::to_lower(head);
	auto wanted = riwo::strtls::to_lower(name);
	return lower.find("\r\n" + wanted + ":") != std::string::npos;
}

[[nodiscard]] std::size_t content_length(std::string_view head)
{
	auto lower = riwo::strtls::to_lower(head);
	const auto mark = lower.find("\r\ncontent-length:");
	if( mark == std::string::npos ) return 0;
	const auto begin = mark + std::string_view("\r\ncontent-length:").size();
	const auto end = lower.find("\r\n", begin);
	return static_cast<std::size_t>(std::stoul(
		std::string(lower.substr(begin, end - begin))));
}

[[nodiscard]] riwo::awaitable<void> discard(http::body_reader &reader)
{
	std::array<std::byte,1024> buffer {};
	for(;;)
	{
		const auto read = co_await reader.read_some(riwo::buffer(buffer),
			riwo::use_awaitable);
		if( read.eof ) break;
	}
}

void protocol_regressions()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get,http::method::head>("/health",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("ok"), riwo::use_awaitable);
		})
		.on_request<http::method::post>("/echo",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await discard(exchange.request_body());
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("echo"), riwo::use_awaitable);
		})
		.on_request<http::method::get,http::method::head>("/download",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				http::body_spec::head(5), riwo::use_awaitable);
		})
		.start();

	const auto port = service.acceptor().local_endpoint().port();
	std::jthread thread([&] { context.run(); });

	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send("GET /health HTTP/1.1\r\n\r\n");
		RIWO_TEST_CHECK_EQ(status_of(client.read_head()), 400);
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send("GET /health HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n");
		RIWO_TEST_CHECK_EQ(status_of(client.read_head()), 400);
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"GET http://127.0.0.1:{}/health HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n",
			port, port));
		const auto head = client.read_head();
		RIWO_TEST_CHECK_EQ(status_of(head), 200);
		RIWO_TEST_CHECK_EQ(client.read_body(content_length(head)), "ok");
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"OPTIONS * HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n", port));
		const auto head = client.read_head();
		RIWO_TEST_CHECK_EQ(status_of(head), 200);
		RIWO_TEST_CHECK(has_header(head, "Allow"));
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"POST /health HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n"
			"Content-Length: 0\r\n\r\n", port));
		const auto head = client.read_head();
		RIWO_TEST_CHECK_EQ(status_of(head), 405);
		RIWO_TEST_CHECK(has_header(head, "Allow"));
		RIWO_TEST_CHECK(content_length(head) != 0);
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"POST /echo HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n"
			"Content-Length: 4\r\nExpect: 100-continue\r\n\r\ntest", port));
		RIWO_TEST_CHECK_EQ(status_of(client.read_head()), 100);
		const auto head = client.read_head();
		RIWO_TEST_CHECK_EQ(status_of(head), 200);
		RIWO_TEST_CHECK_EQ(client.read_body(content_length(head)), "echo");
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"HEAD /download HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n", port));
		const auto head = client.read_head();
		RIWO_TEST_CHECK_EQ(status_of(head), 200);
		RIWO_TEST_CHECK_EQ(content_length(head), 5U);
		RIWO_TEST_CHECK_EQ(client.buffered(), 0U);
	}
	{
		riwo::io_context_t client_context;
		raw_client client(client_context, port);
		client.send(std::format(
			"GET /health HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n"
			"Bad Name: value\r\n\r\n", port));
		RIWO_TEST_CHECK_EQ(status_of(client.read_head()), 400);
	}

	service.stop();
	context.stop();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP/1 protocol regressions", protocol_regressions}
	});
}
