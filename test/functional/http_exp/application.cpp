// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>
#include <array>

namespace
{

namespace http = riwo::http_exp;

struct note_store
{
	std::string note;
	size_t writes = 0;
	size_t reads = 0;
};

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader_t &reader)
{
	std::string result;
	std::array<char,128> buffer {};
	for(;;)
	{
		const auto read = co_await reader.read_some(
			riwo::buffer(buffer), riwo::use_awaitable);
		result.append(buffer.data(), read.bytes);
		if( read.eof ) break;
	}
	co_return result;
}

[[nodiscard]] std::string buffered_body(const http::reply_t &reply)
{
	const auto bytes = reply.buffered_body();
	return {
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size()
	};
}

void note_service_workflow()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.request_limits.max_body_size = 1024;
	http::server_t service(std::move(acceptor), server_config);
	note_store store;
	bool redirect_cookie_received = false;
	bool explicit_cookie_authoritative = false;
	bool disabled_cookie_received = false;
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/health",
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			RIWO_TEST_CHECK_EQ(exchange.request().target, "/health?verbose=1");
			http::response_head head;
			head.headers.add("Content-Type", "application/json");
			co_await exchange.respond(std::move(head),
				std::string_view(R"({"status":"ok"})"), riwo::use_awaitable);
		})
		.on_request<http::method::post>("/notes",
		[&store](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			const auto authorization =
				exchange.request().headers.find_first("authorization");
			if( !authorization || *authorization != "Bearer demo-token" )
			{
				co_await exchange.request_body().discard(riwo::use_awaitable);
				http::response_head head {http::status::unauthorized, {}};
				head.headers.add("WWW-Authenticate", "Bearer");
				co_await exchange.respond(std::move(head),
					std::string_view("authentication required"),
					riwo::use_awaitable);
				co_return;
			}

			store.note = co_await read_body(exchange.request_body());
			++store.writes;
			http::response_head head {http::status::created, {}};
			head.headers.add("Location", "/notes/1");
			head.headers.add("Content-Type", "text/plain");
			co_await exchange.respond(std::move(head), store.note,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/notes/1",
		[&store](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			++store.reads;
			if( store.note.empty() )
			{
				co_await exchange.respond({http::status::not_found, {}},
					http::body_spec::empty(), riwo::use_awaitable);
				co_return;
			}
			http::response_head head;
			head.headers.add("ETag", R"("note-1")");
			co_await exchange.respond(std::move(head), store.note,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/events",
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/event-stream");
			co_await exchange.respond(std::move(head),
				http::body_spec::streaming({{"X-Event-Count"}}),
				riwo::use_awaitable);
			for( const std::string_view event :
				{"data: created\n\n", "data: indexed\n\n"} )
			{
				(void)co_await exchange.response_body().write(
					riwo::buffer(event), riwo::use_awaitable);
			}
			http::fields trailers;
			trailers.add("X-Event-Count", "2");
			co_await exchange.response_body().finish(
				std::move(trailers), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/start",
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			http::response_head head {http::status::found, {}};
			head.headers.add("Set-Cookie", "bad name=ignored");
			http::add_set_cookie(head.headers, {
				.name = "redirect", .value = riwo::value("ready"),
				.path = "/cookies", .http_only = true});
			head.headers.add("Location", "/cookies/target");
			co_await exchange.respond(std::move(head),
				std::string_view("redirecting"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/target",
		[&redirect_cookie_received](http::server_exchange_t &exchange)
			-> riwo::awaitable<void>
		{
			auto parsed = http::parse_cookie_fields(exchange.request().headers);
			redirect_cookie_received = parsed and
				std::ranges::any_of(*parsed, [](const http::cookie_pair &item) {
					return item.name == "redirect" and item.value == "ready";
				});
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("cookie redirect complete"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/explicit",
		[&explicit_cookie_authoritative](http::server_exchange_t &exchange)
			-> riwo::awaitable<void>
		{
			auto parsed = http::parse_cookie_fields(exchange.request().headers);
			explicit_cookie_authoritative = parsed and parsed->size() == 1 and
				parsed->front().name == "manual" and
				parsed->front().value == "one";
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("explicit"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/open-set",
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			http::add_set_cookie(head.headers, {
				.name = "stream", .value = riwo::value("captured"),
				.path = "/cookies"});
			co_await exchange.respond(std::move(head),
				std::string_view("open"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/disabled-set",
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			http::add_set_cookie(head.headers, {
				.name = "disabled", .value = riwo::value("unexpected"),
				.path = "/cookies"});
			co_await exchange.respond(std::move(head),
				std::string_view("set"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cookies/disabled-show",
		[&disabled_cookie_received](http::server_exchange_t &exchange)
			-> riwo::awaitable<void>
		{
			disabled_cookie_received =
				exchange.request().headers.find_first("Cookie").has_value();
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("disabled"), riwo::use_awaitable);
		})
		.on_default(
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::not_found, {}},
				std::string_view("route not found"), riwo::use_awaitable);
		})
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	client_config.pool.idle_timeout = std::chrono::milliseconds(1);
	http::client_t client(context, client_config);
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto health = co_await client.fetch(http::request(http::method::get,
			base + "/health?verbose=1"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(health.status(), http::status::ok);
		RIWO_TEST_CHECK_EQ(health.selected_protocol(), http::protocol::v1_1);
		RIWO_TEST_CHECK_EQ(health.headers().find_first("content-type").value(),
			"application/json");
		RIWO_TEST_CHECK_EQ(buffered_body(health), R"({"status":"ok"})");

		http::request unauthorized(http::method::post, base + "/notes");
		unauthorized.body("private draft");
		auto rejected = co_await client.fetch(
			std::move(unauthorized), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(rejected.status(), http::status::unauthorized);
		RIWO_TEST_CHECK_EQ(
			rejected.headers().find_first("www-authenticate").value(), "Bearer");
		RIWO_TEST_CHECK_EQ(buffered_body(rejected), "authentication required");

		http::request create(http::method::post, base + "/notes");
		create.headers().add("Authorization", "Bearer demo-token");
		create.headers().add("Content-Type", "text/plain");
		create.body("buy milk");
		auto created = co_await client.fetch(
			std::move(create), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(created.status(), http::status::created);
		RIWO_TEST_CHECK_EQ(created.headers().find_first("location").value(),
			"/notes/1");
		RIWO_TEST_CHECK_EQ(buffered_body(created), "buy milk");

		auto fetched = co_await client.fetch(http::request(http::method::get,
			base + "/notes/1"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(fetched.status(), http::status::ok);
		RIWO_TEST_CHECK_EQ(fetched.headers().find_first("etag").value(),
			R"("note-1")");
		RIWO_TEST_CHECK_EQ(buffered_body(fetched), "buy milk");

		auto events = co_await client.send(http::request(http::method::get,
			base + "/events"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(co_await read_body(events.body()),
			"data: created\n\ndata: indexed\n\n");
		auto trailers = events.body().trailers();
		RIWO_TEST_CHECK_EQ(trailers.find_first("x-event-count").value(), "2");

		auto redirected = co_await client.fetch(http::request(http::method::get,
			base + "/cookies/start"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(redirected.status(), http::status::ok);
		RIWO_TEST_CHECK_EQ(buffered_body(redirected), "cookie redirect complete");
		RIWO_TEST_CHECK(redirect_cookie_received);
		RIWO_TEST_CHECK_EQ(client.cookie_store()->size(), 1U);

		http::request explicit_cookie(http::method::get,
			base + "/cookies/explicit");
		explicit_cookie.headers().add("Cookie", "manual=one");
		(void)co_await client.fetch(std::move(explicit_cookie),
			riwo::use_awaitable);
		RIWO_TEST_CHECK(explicit_cookie_authoritative);

		auto open = co_await client.open(http::method::get,
			base + "/cookies/open-set", http::body_spec::empty(),
			riwo::use_awaitable);
		auto open_head = co_await open.wait_response(riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(open_head.status, http::status::ok);
		co_await open.response_body().discard(riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(client.cookie_store()->size(), 2U);

		auto disabled_config = client_config;
		disabled_config.cookies.enabled = false;
		http::client_t disabled_client(context, disabled_config);
		RIWO_TEST_CHECK(!disabled_client.cookie_store());
		(void)co_await disabled_client.fetch(http::request(http::method::get,
			base + "/cookies/disabled-set"), riwo::use_awaitable);
		(void)co_await disabled_client.fetch(http::request(http::method::get,
			base + "/cookies/disabled-show"), riwo::use_awaitable);
		RIWO_TEST_CHECK(!disabled_cookie_received);

		auto missing = co_await client.fetch(http::request(http::method::get,
			base + "/missing"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(missing.status(), http::status::not_found);
		RIWO_TEST_CHECK_EQ(buffered_body(missing), "route not found");

		RIWO_TEST_CHECK_EQ(store.writes, 1U);
		RIWO_TEST_CHECK_EQ(store.reads, 1U);
		service.stop();
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
		{"note service workflow", note_service_workflow}
	});
}
