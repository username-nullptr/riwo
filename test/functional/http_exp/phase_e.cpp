// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>
#include <array>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader &reader)
{
	std::string result;
	std::array<char,8192> buffer {};
	for(;;)
	{
		auto read = co_await reader.read_some(
			riwo::buffer(buffer), riwo::use_awaitable);
		result.append(buffer.data(), read.bytes);
		if( read.eof ) break;
	}
	co_return result;
}

[[nodiscard]] std::string text(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void prior_knowledge_server_and_multiplexing()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	bool echo_started = false;
	bool echo_finished = false;
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::post>("/echo",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			echo_started = true;
			RIWO_TEST_CHECK_EQ(exchange.selected_protocol(), http::protocol::v2);
			auto body = co_await read_body(exchange.request_body());
			auto trailers = exchange.request_body().trailers();
			RIWO_TEST_CHECK_EQ(trailers.find_first("x-client-trailer"), "done");
			http::response_head head;
			head.headers.add("x-protocol", "h2");
			co_await exchange.respond(std::move(head),
				http::body_spec::streaming({{"x-server-trailer"}}),
				riwo::use_awaitable);
			(void)co_await exchange.response_body().write(
				riwo::buffer(body), riwo::use_awaitable);
			http::fields response_trailers;
			response_trailers.add("x-server-trailer", "done");
			riwo::error_code finish_error;
			exchange.response_body().finish(
				std::move(response_trailers), finish_error);
			RIWO_TEST_CHECK(!finish_error);
			echo_finished = true;
		})
		.on_request<http::method::get>("/slow",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({}, http::body_spec::streaming(),
				riwo::use_awaitable);
			const std::string chunk(16 * 1024, 's');
			for( size_t index = 0; index < 16; ++index )
				(void)co_await exchange.response_body().write(
					riwo::buffer(chunk), riwo::use_awaitable);
			co_await exchange.response_body().finish({}, riwo::use_awaitable);
		})
		.on_request<http::method::get>("/fast",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({}, std::string_view("fast"),
				riwo::use_awaitable);
		})
		.on_server_error([](riwo::error_code error) {
			std::cerr << "phase-e server error: " << error.message() << '\n';
			return false;
		})
		.on_service_error([](http::server_exchange &,
			const std::exception &error) {
			std::cerr << "phase-e service error: " << error.what() << '\n';
			return false;
		})
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	http::client_config config;
	config.protocols.requirement = http::protocol_requirement::require_http2;
	config.pool.max_sessions_per_origin = 1;
	http::client client(context, config);

	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		const std::string upload(128 * 1024, 'u');
		http::request request(http::method::post, base + "/echo");
		request.body(riwo::buffer(upload));
		http::fields trailers;
		trailers.add("x-client-trailer", "done");
		request.trailers(std::move(trailers));
		http::reply echo;
		try {
			echo = co_await client.fetch(std::move(request), riwo::use_awaitable);
		}
		catch(const std::exception &error) {
			throw std::runtime_error(std::string("echo fetch: ") + error.what() +
				"; started=" + (echo_started ? "yes" : "no") +
				"; finished=" + (echo_finished ? "yes" : "no"));
		}
		RIWO_TEST_CHECK_EQ(echo.selected_protocol(), http::protocol::v2);
		RIWO_TEST_CHECK_EQ(echo.headers().find_first("x-protocol"), "h2");
		RIWO_TEST_CHECK_EQ(text(echo.buffered_body()), upload);
		try {
			RIWO_TEST_CHECK_EQ(echo.body().trailers().find_first("x-server-trailer"),
				"done");
		}
		catch(const std::exception &error) {
			throw std::runtime_error(std::string("echo trailers: ") + error.what());
		}

		// Leave the large response unread so its stream window closes. A second
		// stream on the same Session must still reach the router and complete.
		auto slow = co_await client.send(
			http::request(http::method::get, base + "/slow"),
			riwo::use_awaitable);
		auto fast = co_await client.fetch(
			http::request(http::method::get, base + "/fast"),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(text(fast.buffered_body()), "fast");
		auto slow_body = co_await read_body(slow.body());
		RIWO_TEST_CHECK_EQ(slow_body.size(), 256U * 1024U);
		service.stop();
		context.stop();
		co_return;
	}, asio::use_future);

	riwo::ignore_unused(context.run_for(std::chrono::seconds(10)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

void invalid_resource_configuration()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config config;
	config.protocols.requirement = http::protocol_requirement::require_http2;
	config.h2.initial_stream_window = 128 * 1024;
	config.h2.inbound_stream_buffer = 64 * 1024;
	http::server service(std::move(acceptor), config);
	service.bind({riwo::ip_type::v4, 0});
	riwo::error_code error;
	service.start(error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));
}

void graceful_shutdown_keeps_active_stream()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/shutdown",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			service.stop();
			co_await exchange.respond({}, std::string_view("completed"),
				riwo::use_awaitable);
		})
		.start();
	const auto target = std::format("http://127.0.0.1:{}/shutdown",
		service.acceptor().local_endpoint().port());
	http::client_config config;
	config.protocols.requirement = http::protocol_requirement::require_http2;
	http::client client(context, config);
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto reply = co_await client.fetch(
			http::request(http::method::get, target), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(reply.selected_protocol(), http::protocol::v2);
		RIWO_TEST_CHECK_EQ(text(reply.buffered_body()), "completed");
		context.stop();
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
		{"HTTP/2 server prior knowledge and multiplexing",
			prior_knowledge_server_and_multiplexing},
		{"HTTP/2 server resource validation", invalid_resource_configuration},
		{"HTTP/2 server graceful shutdown",
			graceful_shutdown_keeps_active_stream}
	});
}
