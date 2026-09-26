// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo.h>
#include <riwo/websocket/protocol/generator.h>
#include <riwo/websocket/protocol/handshake.h>
#include <riwo/websocket/protocol/parser.h>

namespace
{

namespace ws = riwo::websocket;

static_assert(std::same_as<ws::stream::executor_t, asio::any_io_executor>);
static_assert(riwo::test::canonical_executor_type<ws::stream>);
static_assert(riwo::test::canonical_executor_type<ws::client>);
static_assert(riwo::test::canonical_executor_type<ws::open_diagnostics>);
static_assert(riwo::test::canonical_executor_type<ws::accept_result>);
static_assert(riwo::test::canonical_executor_type<ws::server>);
static_assert(riwo::test::canonical_executor_type<ws::retry_open_result>);
static_assert(std::movable<ws::stream>);
static_assert(not std::copy_constructible<ws::stream>);
static_assert(std::movable<ws::client>);
static_assert(not std::copy_constructible<ws::client>);
static_assert(std::movable<ws::retry_open_result>);
static_assert(not std::copy_constructible<ws::retry_open_result>);
static_assert(requires(const ws::retry_open_result &value) {
	{ value.get_executor() } -> std::same_as<ws::retry_open_result::executor_t>;
});
static_assert(std::is_error_code_enum_v<ws::errc>);
static_assert(std::is_error_code_enum_v<ws::protocol_errc>);

struct void_control_callback {
	void operator()(ws::ctrl_payload&) const {}
};

struct bool_control_callback {
	bool operator()(ws::ctrl_payload&) const { return true; }
};

struct invalid_control_callback {
	void operator()() const {}
};

struct async_control_callback {
	riwo::awaitable<int> operator()(ws::ctrl_payload&) const
	{
		co_return 1;
	}
};

template <typename Func>
concept ping_callback = requires(ws::stream &stream, Func callback) {
	stream.on_ping(callback);
};

static_assert(ping_callback<void_control_callback>);
static_assert(ping_callback<bool_control_callback>);
static_assert(ping_callback<async_control_callback>);
static_assert(not ping_callback<invalid_control_callback>);

void umbrella_and_value_types()
{
	ws::stream_config stream_config;
	RIWO_TEST_CHECK(stream_config.max_frame_size > 0);
	RIWO_TEST_CHECK(stream_config.max_message_size > 0);
	RIWO_TEST_CHECK(stream_config.read_buffer_size > 0);
	RIWO_TEST_CHECK_EQ(stream_config.compression.level, -1);
	RIWO_TEST_CHECK_EQ(stream_config.compression.min_message_size, 0U);
	RIWO_TEST_CHECK_EQ(stream_config.ping_interval,
		std::chrono::seconds(5));
	RIWO_TEST_CHECK_EQ(stream_config.pong_timeout_retries, 0U);
	ws::message_chunk chunk;
	RIWO_TEST_CHECK(chunk.body.size() == 0);
	ws::message_info info;
	RIWO_TEST_CHECK_EQ(info.size, 0U);
	ws::ctrl_payload control("ping");
	RIWO_TEST_CHECK_EQ(control.text(), "ping");
	RIWO_TEST_CHECK_EQ(control.bytes().size(), size_t {4});
	RIWO_TEST_CHECK_EQ(control.as_const_buffer().size(), size_t {4});
	control.bytes().front() = std::byte {'P'};
	RIWO_TEST_CHECK_EQ(control.text(), "Ping");
	control.assign("pong payload");
	RIWO_TEST_CHECK_EQ(control.text(), "pong payload");
	RIWO_TEST_CHECK_EQ(control.as_mutable_buffer().size(), control.size());
	ws::client_config client_config;
	RIWO_TEST_CHECK(client_config.no_delay.has_value());
	RIWO_TEST_CHECK(*client_config.no_delay);
	client_config.no_delay = riwo::nullopt;
	RIWO_TEST_CHECK(not client_config.no_delay.has_value());

	ws::connect_request request("https://example.test/socket?mode=public");
	request.subprotocols = {"public.v1"};
	request.max_redirects = 2;
	RIWO_TEST_CHECK_EQ(request.endpoint.protocol(), "https");
	RIWO_TEST_CHECK_EQ(request.subprotocols.front(), "public.v1");

	ws::upgrade_options upgrade;
	upgrade.supported_subprotocols = request.subprotocols;
	upgrade.require_subprotocol = true;
	RIWO_TEST_CHECK(upgrade.require_subprotocol);
	upgrade.subprotocol_selector = [](const ws::request_info &request,
		std::span<const std::string> offered) -> riwo::optional<std::string>
	{
		return request.path == "/socket" and not offered.empty() ?
			riwo::optional<std::string>(offered.front()) : riwo::nullopt;
	};
	upgrade.extension_selector = [](const ws::request_info&,
		std::span<const ws::extension>) -> std::vector<ws::extension>
	{
		return {};
	};
	RIWO_TEST_CHECK(upgrade.subprotocol_selector);
	RIWO_TEST_CHECK(upgrade.extension_selector);

	ws::upgrade_options async_upgrade;
	async_upgrade.async_subprotocol_selector = [](const ws::request_info&,
		std::span<const std::string>)
		-> riwo::awaitable<riwo::optional<std::string>>
	{
		co_return riwo::nullopt;
	};
	async_upgrade.async_extension_selector = [](const ws::request_info&,
		std::span<const ws::extension>)
		-> riwo::awaitable<std::vector<ws::extension>>
	{
		co_return std::vector<ws::extension>{};
	};
	RIWO_TEST_CHECK(async_upgrade.async_subprotocol_selector);
	RIWO_TEST_CHECK(async_upgrade.async_extension_selector);

	auto compression = ws::permessage_deflate_extension();
	RIWO_TEST_CHECK(ws::is_permessage_deflate_extension(compression));
	std::swap(compression.parameters[0], compression.parameters[1]);
	RIWO_TEST_CHECK(ws::is_permessage_deflate_extension(compression));
	compression.parameters.front().value = "1";
	RIWO_TEST_CHECK(not ws::is_permessage_deflate_extension(compression));

	ws::permessage_deflate_options compression_options;
	compression_options.server_max_window_bits = 12;
	compression_options.client_max_window_bits = 10;
	compression_options.offer_client_max_window_bits = true;
	auto negotiated_compression = ws::permessage_deflate_extension(
		compression_options);
	RIWO_TEST_CHECK(ws::is_permessage_deflate_extension(negotiated_compression));
	RIWO_TEST_CHECK_EQ(negotiated_compression.parameters.size(), 2U);
	RIWO_TEST_CHECK_EQ(*negotiated_compression.parameters[0].value, "12");
	RIWO_TEST_CHECK_EQ(*negotiated_compression.parameters[1].value, "10");
	compression_options.client_max_window_bits.reset();
	auto compression_offer = ws::permessage_deflate_extension(compression_options);
	RIWO_TEST_CHECK(not compression_offer.parameters.back().value);

	ws::proxy_config proxy {
		.type = ws::proxy_type::http,
		.endpoint = riwo::url("http://proxy.example:8080"),
	};
	proxy.set_basic_auth("user", "secret");
	RIWO_TEST_CHECK_EQ(proxy.authorization.value_or(""),
		"Basic dXNlcjpzZWNyZXQ=");
	RIWO_TEST_CHECK_EQ(proxy.username.value_or(""), "user");
	proxy.set_bearer_auth("token");
	RIWO_TEST_CHECK_EQ(proxy.authorization.value_or(""), "Bearer token");
	RIWO_TEST_CHECK(not proxy.username);
	ws::client_config proxy_client_config;
	RIWO_TEST_CHECK(std::holds_alternative<ws::use_global_proxy_t>(
		proxy_client_config.default_proxy));
	proxy_client_config.default_proxy = ws::no_proxy;
	RIWO_TEST_CHECK(std::holds_alternative<ws::no_proxy_t>(
		proxy_client_config.default_proxy));
	ws::connect_request inherited("ws://example.test/");
	RIWO_TEST_CHECK(not inherited.proxy);
	ws::retry_open_options retry_options;
	RIWO_TEST_CHECK_EQ(retry_options.max_attempts, 0U);
	RIWO_TEST_CHECK_EQ(ws::suggest_retry_open_delay(retry_options, 0),
		std::chrono::milliseconds::zero());
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	static_assert(ws::permessage_deflate_available_v);
#else
	static_assert(not ws::permessage_deflate_available_v);
#endif

	ws::close_frame close(ws::close_code::normal_closure, "done");
	auto encoded = ws::encode_close_payload(close);
	RIWO_TEST_CHECK(encoded.has_value());
	auto decoded = ws::decode_close_payload(encoded->buffer());
	RIWO_TEST_CHECK(decoded.has_value());
	RIWO_TEST_CHECK_EQ(decoded->code.value_or(0), 1000);
	RIWO_TEST_CHECK_EQ(decoded->reason, "done");
}

void executor_bound_public_objects()
{
	riwo::io_context_t context;
	ws::stream stream(context.get_executor());
	stream.on_ping([](ws::ctrl_payload&) { return 17; })
		.on_pong([](ws::ctrl_payload&) -> riwo::awaitable<std::string> {
			co_return "ignored";
		})
		.on_closed([](const ws::close_info&) {});
	RIWO_TEST_CHECK_EQ(stream.state(), ws::connection_state::idle);
	RIWO_TEST_CHECK(stream.get_executor() == context.get_executor());

	ws::client client(context.get_executor());
	RIWO_TEST_CHECK(client.get_executor() == context.get_executor());
	RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);

	asio::ip::tcp::acceptor acceptor(context);
	ws::server server(std::move(acceptor));
	RIWO_TEST_CHECK(server.get_executor() == context.get_executor());
	RIWO_TEST_CHECK_EQ(server.pending_accept_count(), 0U);
	RIWO_TEST_CHECK_EQ(server.pending_handshake_count(), 0U);
}

void asynchronous_completion_signatures()
{
	riwo::io_context_t context;
	ws::stream stream(context.get_executor());

	bool read_completed = false;
	stream.read<std::string>([&](riwo::error_code error,
		ws::basic_message<std::string> message)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK(message.body.empty());
		read_completed = true;
	});

	bool write_completed = false;
	stream.write_text("public", [&](riwo::error_code error, size_t written)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK_EQ(written, 0U);
		write_completed = true;
	});

	bool frame_completed = false;
	stream.read_frame<std::string>([&](riwo::error_code error,
		ws::basic_data_frame<std::string> frame)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK(frame.body.empty());
		frame_completed = true;
	});

	bool frame_write_completed = false;
	const ws::basic_data_frame<std::string> outgoing_frame {
		.type = ws::message_type::text,
		.body = "frame",
	};
	stream.write_frame(outgoing_frame,
	[&](riwo::error_code error, size_t written)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK_EQ(written, 0U);
		frame_write_completed = true;
	});

	bool consume_completed = false;
	stream.consume([](const ws::message_chunk&) {},
	[&](riwo::error_code error, ws::message_info info)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK_EQ(info.size, 0U);
		consume_completed = true;
	});

	bool close_completed = false;
	stream.close([&](riwo::error_code error, ws::close_info info)
	{
		RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
		RIWO_TEST_CHECK(not info.clean);
		close_completed = true;
	});

	RIWO_TEST_CHECK(not read_completed);
	RIWO_TEST_CHECK(not write_completed);
	RIWO_TEST_CHECK(not frame_completed);
	RIWO_TEST_CHECK(not frame_write_completed);
	RIWO_TEST_CHECK(not consume_completed);
	RIWO_TEST_CHECK(not close_completed);
	context.run();
	RIWO_TEST_CHECK(read_completed);
	RIWO_TEST_CHECK(write_completed);
	RIWO_TEST_CHECK(frame_completed);
	RIWO_TEST_CHECK(frame_write_completed);
	RIWO_TEST_CHECK(consume_completed);
	RIWO_TEST_CHECK(close_completed);
}

void retry_open_completion_signature()
{
	riwo::io_context_t context;
	ws::client client(context.get_executor());
	ws::retry_open_options config;
	config.max_attempts = 1;
	config.jitter = 0.0;

	bool completed = false;
	ws::retry_open(client,
		ws::connect_request("ftp://example.test/socket"), config,
		[&](riwo::error_code error, ws::retry_open_result result)
		{
			RIWO_TEST_CHECK_EQ(error,
				std::make_error_code(std::errc::protocol_not_supported));
			RIWO_TEST_CHECK_EQ(result.attempts, 1U);
			RIWO_TEST_CHECK_EQ(result.last_failure.error, error);
			RIWO_TEST_CHECK(not result.stream.is_open());
			RIWO_TEST_CHECK(result.stream.get_executor() ==
				context.get_executor());
			completed = true;
		});
	RIWO_TEST_CHECK(not completed);
	context.run();
	RIWO_TEST_CHECK(completed);

	context.restart();
	config.jitter = 0.0;
	completed = false;
	size_t factory_calls = 0;
	ws::retry_open(client,
		[&](const ws::retry_open_context &previous)
			-> riwo::awaitable<ws::connect_request>
		{
			RIWO_TEST_CHECK_EQ(previous.attempt, 0U);
			++factory_calls;
			co_return ws::connect_request("ftp://example.test/socket");
		}, config,
		[&](riwo::error_code error, ws::retry_open_result result)
		{
			RIWO_TEST_CHECK_EQ(error,
				std::make_error_code(std::errc::protocol_not_supported));
			RIWO_TEST_CHECK_EQ(result.attempts, 1U);
			completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK_EQ(factory_calls, 1U);

	context.restart();
	config.jitter = 1.1;
	completed = false;
	ws::retry_open(client,
		ws::connect_request("ws://example.test/socket"), config,
		[&](riwo::error_code error, ws::retry_open_result result)
		{
			RIWO_TEST_CHECK_EQ(error,
				std::make_error_code(std::errc::invalid_argument));
			RIWO_TEST_CHECK_EQ(result.attempts, 0U);
			completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(completed);
}

void non_default_constructible_executor_errors()
{
	riwo::io_context_t context;
	using executor_t = asio::strand<riwo::io_context_t::executor_type>;
	auto executor = asio::make_strand(context);
	ws::basic_client<executor_t> client(executor);

	riwo::error_code error;
	auto stream = client.open(ws::connect_request(
		"ftp://example.test/socket"), error);
	RIWO_TEST_CHECK_EQ(error,
		std::make_error_code(std::errc::protocol_not_supported));
	RIWO_TEST_CHECK_EQ(stream.state(), ws::connection_state::idle);
	RIWO_TEST_CHECK(stream.get_executor() == executor);

	bool completed = false;
	ws::connect_request expired("ws://example.test/socket");
	expired.handshake_timeout = std::chrono::milliseconds::zero();
	client.open(std::move(expired),
		[&](riwo::error_code callback_error,
			ws::basic_stream<executor_t> callback_stream)
		{
			RIWO_TEST_CHECK_EQ(callback_error,
				riwo::error_code(asio::error::timed_out));
			RIWO_TEST_CHECK_EQ(callback_stream.state(),
				ws::connection_state::idle);
			RIWO_TEST_CHECK(callback_stream.get_executor() == executor);
			completed = true;
		});
	RIWO_TEST_CHECK(not completed);
	context.run();
	RIWO_TEST_CHECK(completed);

	context.restart();
	bool retry_completed = false;
	ws::retry_open_options options;
	options.max_attempts = 1;
	ws::retry_open(client,
		ws::connect_request("ftp://example.test/socket"), options,
		[&](riwo::error_code callback_error,
			ws::basic_retry_open_result<executor_t> result)
		{
			RIWO_TEST_CHECK_EQ(callback_error,
				std::make_error_code(std::errc::protocol_not_supported));
			RIWO_TEST_CHECK_EQ(result.attempts, 1U);
			RIWO_TEST_CHECK(result.stream.get_executor() == executor);
			retry_completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(retry_completed);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"umbrella and value types", umbrella_and_value_types},
		{"executor-bound public objects", executor_bound_public_objects},
		{"asynchronous completion signatures", asynchronous_completion_signatures},
		{"retry open completion signature",
			retry_open_completion_signature},
		{"non-default-constructible executor errors",
			non_default_constructible_executor_errors},
	});
}
