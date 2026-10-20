// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/websocket_exp.h>
#include <riwo/websocket_exp/protocol/generator.h>
#include <riwo/websocket_exp/protocol/handshake.h>
#include <riwo/websocket_exp/protocol/parser.h>

namespace
{

namespace http = riwo::http_exp;
namespace ws = riwo::websocket_exp;

using native_executor = riwo::io_context_t::executor_type;
using native_stream = ws::basic_stream<native_executor>;
using native_client = ws::basic_client<native_executor>;
using native_diagnostics = ws::basic_open_diagnostics<native_executor>;
using native_accept_result = ws::basic_accept_result<native_executor>;
using native_retry_result = ws::basic_retry_open_result<native_executor>;

static_assert(riwo::test::canonical_executor_type<ws::stream>);
static_assert(riwo::test::canonical_executor_type<ws::client>);
static_assert(riwo::test::canonical_executor_type<ws::open_diagnostics>);
static_assert(riwo::test::canonical_executor_type<ws::accept_result>);
static_assert(riwo::test::canonical_executor_type<ws::retry_open_result>);
static_assert(std::same_as<ws::stream::connection_t,http::duplex_channel>);
static_assert(std::same_as<native_stream::connection_t,
	http::basic_duplex_channel<native_executor>>);
static_assert(std::movable<ws::stream>);
static_assert(not std::copy_constructible<ws::stream>);
static_assert(std::movable<ws::client>);
static_assert(not std::copy_constructible<ws::client>);
static_assert(std::movable<ws::retry_open_result>);
static_assert(not std::copy_constructible<ws::retry_open_result>);
static_assert(std::is_error_code_enum_v<ws::errc>);
static_assert(std::is_error_code_enum_v<ws::protocol_errc>);

struct control_callback
{
	void operator()(ws::ctrl_payload &payload) const {
		payload = "rewritten";
	}
};

static_assert(ws::stream::control_callback_v<control_callback>);
static_assert(requires(ws::stream &stream, riwo::error_code &error) {
	stream.write_text("message", error);
	stream.write_binary(riwo::buffer(std::string_view("message")), error);
	stream.ping(riwo::buffer(std::string_view("ping")), error);
	stream.pong(error);
	stream.close(ws::close_frame {}, error);
	stream.cancel(error);
	stream.shutdown(error);
});

void umbrella_and_value_types()
{
	ws::ctrl_payload payload("hello");
	RIWO_TEST_CHECK_EQ(payload.text(), "hello");
	payload = "updated";
	RIWO_TEST_CHECK_EQ(payload.size(), 7U);

	ws::close_frame close(ws::close_code::normal_closure, "complete");
	RIWO_TEST_CHECK_EQ(close.code, 1000U);
	RIWO_TEST_CHECK_EQ(close.reason, "complete");

	const auto compression = ws::permessage_deflate_extension({
		.server_no_context_takeover = true,
		.offer_client_max_window_bits = true,
	});
	RIWO_TEST_CHECK(ws::is_permessage_deflate_extension(compression));
	RIWO_TEST_CHECK_EQ(compression.name, "permessage-deflate");

	ws::connect_request request;
	request.endpoint = riwo::url("ws://example.test/chat");
	request.headers.add("Authorization", "Bearer token");
	request.subprotocols = {"chat.v1"};
	request.extensions = {compression};
	RIWO_TEST_CHECK_EQ(request.endpoint.protocol(), "ws");
	RIWO_TEST_CHECK_EQ(request.headers.find_first("authorization").value(),
		"Bearer token");

	ws::upgrade_options upgrade;
	upgrade.supported_subprotocols = {"chat.v1"};
	upgrade.response_headers.add("X-WebSocket", "accepted");
	upgrade.request_validator = [](const ws::request_info &info) {
		RIWO_TEST_CHECK_EQ(info.method, http::method::get);
		return ws::upgrade_validation_result {};
	};
	RIWO_TEST_CHECK(upgrade.request_validator);

	ws::retry_open_options retry;
	retry.initial_delay = std::chrono::milliseconds(10);
	retry.max_delay = std::chrono::milliseconds(25);
	retry.multiplier = 2.0;
	retry.jitter = 0.0;
	RIWO_TEST_CHECK_EQ(ws::suggest_retry_open_delay(retry, 0),
		std::chrono::milliseconds::zero());
	RIWO_TEST_CHECK_EQ(ws::suggest_retry_open_delay(retry, 1),
		std::chrono::milliseconds(10));
	RIWO_TEST_CHECK_EQ(ws::suggest_retry_open_delay(retry, 3),
		std::chrono::milliseconds(25));

	ws::retry_open_context retryable;
	retryable.error = asio::error::make_error_code(asio::error::connection_refused);
	auto decision = ws::default_retry_open_decision(retryable,
		std::chrono::milliseconds(12));
	RIWO_TEST_CHECK(decision.retry);
	RIWO_TEST_CHECK_EQ(decision.delay, std::chrono::milliseconds(12));
	retryable.http_status = http::status::forbidden;
	RIWO_TEST_CHECK(not ws::default_retry_open_decision(retryable,
		std::chrono::milliseconds(12)).retry);
}

void executor_bound_objects()
{
	riwo::io_context_t context;
	const auto executor = context.get_executor();

	native_stream stream(executor);
	native_client client(executor);
	native_diagnostics diagnostics;
	native_accept_result accepted(executor);
	native_retry_result retried(executor);

	RIWO_TEST_CHECK(stream.get_executor() == executor);
	RIWO_TEST_CHECK(client.get_executor() == executor);
	RIWO_TEST_CHECK(client.http_client().get_executor() == executor);
	RIWO_TEST_CHECK(accepted.stream.get_executor() == executor);
	RIWO_TEST_CHECK(retried.get_executor() == executor);
	RIWO_TEST_CHECK_EQ(stream.state(), ws::connection_state::idle);
	RIWO_TEST_CHECK(not stream.is_open());
	RIWO_TEST_CHECK(not diagnostics.response);
	RIWO_TEST_CHECK(not diagnostics.selected_protocol);
}

void invalid_open_completion()
{
	riwo::io_context_t context;
	ws::basic_client<native_executor> client(context.get_executor());
	ws::basic_open_diagnostics<native_executor> diagnostics;
	bool completed = false;
	client.open(riwo::url("ftp://example.test/socket"), diagnostics,
		[&](riwo::error_code error, native_stream stream)
		{
			RIWO_TEST_CHECK_EQ(error,
				std::make_error_code(std::errc::protocol_not_supported));
			RIWO_TEST_CHECK(stream.get_executor() == context.get_executor());
			RIWO_TEST_CHECK_EQ(stream.state(), ws::connection_state::idle);
			RIWO_TEST_CHECK_EQ(diagnostics.endpoint.protocol(), "ftp");
			completed = true;
		});
	RIWO_TEST_CHECK(not completed);
	context.run();
	RIWO_TEST_CHECK(completed);
}

void unopened_stream_completions()
{
	riwo::io_context_t context;
	ws::basic_stream<native_executor> stream(context.get_executor());
	riwo::error_code error;
	RIWO_TEST_CHECK_EQ(stream.write_text("message", error), 0U);
	RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));

	error.clear();
	(void)stream.ping(error);
	RIWO_TEST_CHECK_EQ(error,
		std::make_error_code(std::errc::operation_not_permitted));

	error.clear();
	(void)stream.close(error);
	RIWO_TEST_CHECK_EQ(error, ws::make_error_code(ws::errc::not_open));
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"umbrella and value types", umbrella_and_value_types},
		{"executor-bound public objects", executor_bound_objects},
		{"invalid open completion", invalid_open_completion},
		{"unopened stream completions", unopened_stream_completions},
	});
}
