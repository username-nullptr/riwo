// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>
#include <fstream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader &reader)
{
	std::string result;
	std::array<char,1024> buffer {};
	for(;;)
	{
		auto read = co_await reader.read_some(riwo::buffer(buffer),
			riwo::use_awaitable);
		result.append(buffer.data(), read.bytes);
		if( read.eof ) break;
	}
	co_return result;
}

void h1_client_server_round_trip()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/get",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			const std::string body = "hello-http-exp";
			http::response_head head;
			head.headers.add("X-Riwo-Protocol", "h1");
			co_await exchange.respond(std::move(head), body, riwo::use_awaitable);
		})
		.on_request<http::method::post>("/echo",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto body = co_await read_body(exchange.request_body());
			co_await exchange.respond({http::status::created, {}}, body,
				riwo::use_awaitable);
		})
		.on_request<http::method::post>("/cancel",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			try {
				(void)co_await read_body(exchange.request_body());
			}
			catch(const std::system_error&) {
			}
		})
		.on_request<http::method::get>("/chunks",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				http::body_spec::streaming({{"Digest"}}), riwo::use_awaitable);
			for( size_t index = 0; index < 128; ++index )
			{
				const std::string chunk(1024, static_cast<char>('a' + index % 26));
				(void)co_await exchange.response_body().write(
					riwo::buffer(chunk), riwo::use_awaitable);
			}
			http::fields trailers;
			trailers.add("Digest", "test-value");
			co_await exchange.response_body().finish(
				std::move(trailers), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/reset-content",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto [rejected] = co_await exchange.respond(
				{http::status::reset_content, {}}, std::string_view("forbidden"),
				asio::as_tuple(riwo::use_awaitable));
			RIWO_TEST_CHECK(rejected == http::errc::body_not_allowed);
			co_await exchange.respond({http::status::reset_content, {}},
				http::body_spec::empty(), riwo::use_awaitable);
		})
		.start();

	const auto port = service.acceptor().local_endpoint().port();
	const auto base = std::format("http://127.0.0.1:{}", port);
	http::client_config client_config;
	client_config.pool.idle_timeout = std::chrono::milliseconds(1);
	http::client client(context, client_config);
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto fetched = co_await client.fetch(
			http::request(http::method::get, base + "/get"),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(fetched.status(), http::status::ok);
		RIWO_TEST_CHECK_EQ(fetched.headers().find_first("x-riwo-protocol").value(),
			"h1");
		RIWO_TEST_CHECK_EQ(std::string_view(
			reinterpret_cast<const char*>(fetched.buffered_body().data()),
			fetched.buffered_body().size()), "hello-http-exp");

		auto posted = co_await client.post(base + "/echo",
			"post-body", riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(posted.status(), http::status::created);
		RIWO_TEST_CHECK_EQ(co_await read_body(posted.body()), "post-body");

		auto path = std::filesystem::temp_directory_path() /
			"riwo-http-exp-phase-b-upload.bin";
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << "file-body";
		}
		http::request file_request(http::method::post, base + "/echo");
		file_request.file(path);
		auto file_reply = co_await client.send(
			std::move(file_request), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(co_await read_body(file_reply.body()), "file-body");
		std::filesystem::remove(path);

		auto streamed = co_await client.open(http::method::post, base + "/echo",
			http::body_spec::streaming(), riwo::use_awaitable);
		(void)co_await streamed.request_body().write(
			riwo::buffer(std::string_view("stream-")), riwo::use_awaitable);
		(void)co_await streamed.request_body().write(
			riwo::buffer(std::string_view("body")), riwo::use_awaitable);
		co_await streamed.request_body().finish({}, riwo::use_awaitable);
		auto streamed_head = co_await streamed.wait_response(riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(streamed_head.status, http::status::created);
		RIWO_TEST_CHECK_EQ(co_await read_body(streamed.response_body()),
			"stream-body");

		auto reset = co_await client.fetch(http::request(http::method::get,
			base + "/reset-content"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(reset.status(), http::status::reset_content);
		RIWO_TEST_CHECK(reset.buffered_body().empty());

		auto chunks = co_await client.send(
			http::request(http::method::get, base + "/chunks"),
			riwo::use_awaitable);
		std::array<char,257> slow_buffer {};
		size_t received = 0;
		for(;;)
		{
			auto read = co_await chunks.body().read_some(
				riwo::buffer(slow_buffer), riwo::use_awaitable);
			received += read.bytes;
			if( read.eof ) break;
		}
		RIWO_TEST_CHECK(chunks.body().is_finished());
		RIWO_TEST_CHECK_EQ(received, 128U * 1024U);
		auto trailers = chunks.body().trailers();
		RIWO_TEST_CHECK_EQ(trailers.find_first("digest").value(), "test-value");

		auto cancelled = co_await client.open(http::method::post,
			base + "/cancel", http::body_spec::streaming(), riwo::use_awaitable);
		cancelled.cancel();
		riwo::error_code cancel_error;
		RIWO_TEST_CHECK_EQ(cancelled.request_body().write(
			riwo::buffer(std::string_view("not-written")), cancel_error), 0U);
		RIWO_TEST_CHECK_EQ(cancel_error,
			http::make_error_code(http::errc::operation_not_permitted));
		service.stop();
		co_return;
	}, asio::use_future);
	riwo::ignore_unused(context.run_for(std::chrono::seconds(5)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

void h1_0_and_cancel()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/version",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			RIWO_TEST_CHECK_EQ(exchange.selected_protocol(), http::protocol::v1_0);
			co_await exchange.respond({http::status::ok, {}},
				std::string("http-1.0"), riwo::use_awaitable);
		})
		.start();
	const auto port = service.acceptor().local_endpoint().port();
	http::client_config config;
	config.protocols.preference = {http::protocol::v1_0};
	config.pool.idle_timeout = std::chrono::milliseconds(1);
	http::client client(context, config);
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto result = co_await client.fetch(http::request(http::method::get,
			std::format("http://127.0.0.1:{}/version", port)),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(result.selected_protocol(), http::protocol::v1_0);
		RIWO_TEST_CHECK_EQ(std::string_view(
			reinterpret_cast<const char*>(result.buffered_body().data()),
			result.buffered_body().size()), "http-1.0");
		service.stop();
		co_return;
	}, asio::use_future);
	riwo::ignore_unused(context.run_for(std::chrono::seconds(5)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP/1 client server round trip", h1_client_server_round_trip},
		{"HTTP/1.0 and cancellation", h1_0_and_cancel}
	});
}
