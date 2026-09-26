// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/client.h>
#include <riwo/http/server.h>

namespace
{

void client_server_round_trip()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor));
	auto server_config = service.config();
	server_config.keepalive_time = std::chrono::milliseconds(100);
	service.set_config(server_config);
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/hello/{name}",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			auto &request = request_context.request();
			const auto body = std::format(
				"hello:{}:{}",
				request.path_arg("name").value_or("missing").to_string(),
				request.parameter("value").value_or("missing").to_string()
			);
			request_context.response().set_header("X-Riwo-Test", "loopback");
			co_await request_context.response().write(asio::buffer(body), riwo::use_awaitable);
			co_return;
		})
		.on_request<method::get>("/cookie/set",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			request_context.response().set_cookie(
				"session", cookie("stored").set_path("/")
			);
			constexpr std::string_view body = "set";
			co_await request_context.response().write(asio::buffer(body), riwo::use_awaitable);
			co_return;
		})
		.on_request<method::get>("/cookie/show",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			const auto value = request_context.request().cookie("session")
				.value_or("missing").to_string();
			co_await request_context.response().write(asio::buffer(value), riwo::use_awaitable);
			co_return;
		})
		.on_default([](server::context_t &request_context) -> riwo::awaitable<void>
		{
			request_context.response().set_status(status::not_found);
			constexpr std::string_view body = "not found";
			co_await request_context.response().write(asio::buffer(body), riwo::use_awaitable);
			co_return;
		})
		.start();

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto base = std::format("http://127.0.0.1:{}", port);
	client requester(context.get_executor());
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		try
		{
			auto hello = co_await requester.request_get(
				base + "/hello/Riwo?value=42", riwo::use_awaitable
			);
			RIWO_TEST_CHECK(hello);
			auto socket_options = hello->lease()->options();
			RIWO_TEST_CHECK(socket_options);
			RIWO_TEST_CHECK(socket_options->no_delay);
			RIWO_TEST_CHECK_EQ(
				co_await hello->wait_reply(riwo::use_awaitable), status::ok
			);
			RIWO_TEST_CHECK_EQ(
				hello->reply()->header("X-Riwo-Test")->to_string(), "loopback"
			);
			RIWO_TEST_CHECK_EQ(
				co_await hello->reply()->read<std::string>(riwo::use_awaitable),
				"hello:Riwo:42"
			);

			client_config delayed_config;
			delayed_config.no_delay = false;
			client delayed_requester(context.get_executor(), delayed_config);
			auto delayed = co_await delayed_requester.request_get(
				base + "/hello/delayed?value=off", riwo::use_awaitable
			);
			RIWO_TEST_CHECK(delayed);
			socket_options = delayed->lease()->options();
			RIWO_TEST_CHECK(socket_options);
			RIWO_TEST_CHECK(not socket_options->no_delay);
			RIWO_TEST_CHECK_EQ(
				co_await delayed->wait_reply(riwo::use_awaitable), status::ok
			);
			RIWO_TEST_CHECK_EQ(
				co_await delayed->reply()->read<std::string>(riwo::use_awaitable),
				"hello:delayed:off"
			);

			auto set_cookie = co_await requester.request_get(
				base + "/cookie/set", riwo::use_awaitable
			);
			RIWO_TEST_CHECK_EQ(
				co_await set_cookie->wait_reply(riwo::use_awaitable), status::ok
			);
			RIWO_TEST_CHECK_EQ(
				co_await set_cookie->reply()->read<std::string>(riwo::use_awaitable), "set"
			);

			auto show_cookie = co_await requester.request_get(
				base + "/cookie/show", riwo::use_awaitable
			);
			RIWO_TEST_CHECK_EQ(
				co_await show_cookie->wait_reply(riwo::use_awaitable), status::ok
			);
			RIWO_TEST_CHECK_EQ(
				co_await show_cookie->reply()->read<std::string>(riwo::use_awaitable), "stored"
			);

			auto missing = co_await requester.request_get(
				base + "/missing", riwo::use_awaitable
			);
			RIWO_TEST_CHECK_EQ(
				co_await missing->wait_reply(riwo::use_awaitable), status::not_found
			);
			RIWO_TEST_CHECK_EQ(
				co_await missing->reply()->read<std::string>(riwo::use_awaitable), "not found"
			);

			for(int index = 0; index < 100; ++index)
			{
				auto repeated = co_await requester.request_get(
					base + std::format("/hello/repeat?value={}", index),
					riwo::use_awaitable
				);
				RIWO_TEST_CHECK(repeated);
				RIWO_TEST_CHECK_EQ(
					co_await repeated->wait_reply(riwo::use_awaitable), status::ok
				);
				RIWO_TEST_CHECK_EQ(
					co_await repeated->reply()->read<std::string>(riwo::use_awaitable),
					std::format("hello:repeat:{}", index)
				);
			}
		}
		catch(...)
		{
			service.stop();
			throw;
		}
		service.stop();
		co_return;
	}, asio::use_future);
	context.run();
	completed.get();
}

void runtime_route_updates()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor));
	auto server_config = service.config();
	server_config.keepalive_time = std::chrono::seconds(1);
	service.set_config(server_config);
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/routes/exact",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			const auto body = request_context.request().path_args().empty() ?
				std::string_view("exact") : std::string_view("stale-arguments");
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable
			);
		})
		.on_request<method::get>("/routes/*",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			constexpr std::string_view body = "pattern";
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable
			);
		})
		.on_default([](server::context_t &request_context) -> riwo::awaitable<void>
		{
			request_context.response().set_status(status::not_found);
			constexpr std::string_view body = "not found";
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable
			);
		})
		.start();

	// Route mutation remains available after start().
	service.on_request<method::get>("/runtime-route",
	[](server::context_t &request_context) -> riwo::awaitable<void>
	{
		constexpr std::string_view body = "runtime";
		co_await request_context.response().write(
			asio::buffer(body), riwo::use_awaitable
		);
	});

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto base = std::format("http://127.0.0.1:{}", port);
	client requester(context.get_executor());

	std::atomic_bool begin_updates {false};
	std::atomic_bool updates_started {false};
	std::atomic_bool end_updates {false};
	std::exception_ptr update_error {};
	std::thread updater([&]
	{
		while( not begin_updates.load(std::memory_order_acquire) )
			std::this_thread::yield();

		try
		{
			do
			{
				service.on_request<method::get>("/transient",
				[](server::context_t &request_context) -> riwo::awaitable<void>
				{
					constexpr std::string_view body = "transient";
					co_await request_context.response().write(
						asio::buffer(body), riwo::use_awaitable
					);
				});
				service.unbound_request("/transient");
				updates_started.store(true, std::memory_order_release);
				std::this_thread::yield();
			}
			while( not end_updates.load(std::memory_order_acquire) );
		}
		catch(...)
		{
			update_error = std::current_exception();
			updates_started.store(true, std::memory_order_release);
		}
	});

	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto request_body = [&](std::string target)
			-> riwo::awaitable<std::pair<status_enum,std::string>>
		{
			auto request = co_await requester.request_get(
				base + std::move(target), riwo::use_awaitable
			);
			RIWO_TEST_CHECK(request);
			auto reply_status = co_await request->wait_reply(riwo::use_awaitable);
			auto body = co_await request->reply()->read<std::string>(riwo::use_awaitable);
			co_return std::pair{reply_status, std::move(body)};
		};

		try
		{
			auto exact_result = co_await request_body("/routes/exact");
			auto &[exact_status, exact_body] = exact_result;
			RIWO_TEST_CHECK_EQ(exact_status, status::ok);
			RIWO_TEST_CHECK_EQ(exact_body, "exact");

			auto pattern_result = co_await request_body("/routes/other");
			auto &[pattern_status, pattern_body] = pattern_result;
			RIWO_TEST_CHECK_EQ(pattern_status, status::ok);
			RIWO_TEST_CHECK_EQ(pattern_body, "pattern");

			auto runtime_result = co_await request_body("/runtime-route");
			auto &[runtime_status, runtime_body] = runtime_result;
			RIWO_TEST_CHECK_EQ(runtime_status, status::ok);
			RIWO_TEST_CHECK_EQ(runtime_body, "runtime");

			service.unbound_request("/runtime-route");
			auto removed_result = co_await request_body("/runtime-route");
			auto &[removed_status, removed_body] = removed_result;
			RIWO_TEST_CHECK_EQ(removed_status, status::not_found);
			RIWO_TEST_CHECK_EQ(removed_body, "not found");

			begin_updates.store(true, std::memory_order_release);
			while( not updates_started.load(std::memory_order_acquire) )
				std::this_thread::yield();
			for(size_t index = 0; index < 50; ++index)
			{
				auto request_result = co_await request_body("/routes/exact");
				auto &[status_value, body] = request_result;
				RIWO_TEST_CHECK_EQ(status_value, status::ok);
				RIWO_TEST_CHECK_EQ(body, "exact");
			}
		}
		catch(...)
		{
			begin_updates.store(true, std::memory_order_release);
			end_updates.store(true, std::memory_order_release);
			service.stop();
			throw;
		}
		end_updates.store(true, std::memory_order_release);
		service.stop();
	}, asio::use_future);

	context.run();
	updater.join();
	completed.get();
	if( update_error )
		std::rethrow_exception(update_error);
}

void static_file_cache_updates()
{
	using namespace riwo::http;
	riwo::test::temporary_directory directory;
	const auto file = std::filesystem::absolute(directory.path() / "cached.txt");
	const std::string initial(8 * 1'024, 'a');
	const std::string updated(8 * 1'024, 'b');
	{
		std::ofstream output(file, std::ios::binary);
		output.write(initial.data(), static_cast<std::streamsize>(initial.size()));
	}
	const auto initial_modified = std::filesystem::last_write_time(file);

	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor));
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/cached-file",
		[file](server::context_t &request_context) -> riwo::awaitable<void>
		{
			co_await request_context.response().send_file(
				file, riwo::use_awaitable
			);
		})
		.start();

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	const auto target = std::format(
		"http://127.0.0.1:{}/cached-file", port
	);
	client requester(context.get_executor());
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto fetch = [&]() -> riwo::awaitable<std::string>
		{
			auto request = co_await requester.request_get(
				target, riwo::use_awaitable
			);
			RIWO_TEST_CHECK(request);
			RIWO_TEST_CHECK_EQ(
				co_await request->wait_reply(riwo::use_awaitable), status::ok
			);
			if constexpr( gzip_available_v )
			{
				auto encoding = request->reply()->header(header::content_encoding);
				RIWO_TEST_CHECK(encoding);
				RIWO_TEST_CHECK_EQ(encoding->to_string(), "gzip");
			}
			co_return co_await request->reply()->read<std::string>(
				riwo::use_awaitable
			);
		};

		try
		{
			RIWO_TEST_CHECK_EQ(co_await fetch(), initial);
			RIWO_TEST_CHECK_EQ(co_await fetch(), initial);

			// Same-size replacement invalidates both raw and gzip cache variants.
			{
				std::ofstream output(file, std::ios::binary | std::ios::trunc);
				output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
			}
			std::filesystem::last_write_time(
				file, initial_modified + std::chrono::seconds(2)
			);
			RIWO_TEST_CHECK_EQ(co_await fetch(), updated);
		}
		catch(...)
		{
			service.stop();
			throw;
		}
		service.stop();
	}, asio::use_future);

	context.run();
	completed.get();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"client/server loopback", client_server_round_trip},
		{"runtime route updates", runtime_route_updates},
		{"static file cache updates", static_file_cache_updates},
	});
}
