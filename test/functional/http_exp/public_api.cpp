// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/connection.h>

namespace
{

namespace http = riwo::http_exp;

using native_executor = riwo::io_context_t::executor_type;
using native_body_reader = http::basic_body_reader<native_executor>;
using native_body_writer = http::basic_body_writer<native_executor>;
using native_client_exchange = http::basic_client_exchange<native_executor>;
using native_reply = http::basic_reply<native_executor>;
using native_server_exchange = http::basic_server_exchange<native_executor>;
using native_client = http::basic_client<native_executor>;
using native_server = http::basic_server<native_executor>;
using native_acceptor_wrap = http::basic_acceptor_wrap<native_executor>;
using native_strand = asio::strand<native_executor>;
using split_server = http::basic_server<native_executor,native_strand>;

struct source_model
{
	[[nodiscard]] riwo::optional<uint64_t> size() const noexcept { return 3; }
	[[nodiscard]] bool replayable() const noexcept { return true; }
	void read_some(riwo::mutable_buffer,
		asio::any_completion_handler<void(riwo::error_code, size_t)> handler)
	{
		std::move(handler)(riwo::error_code{}, 0);
	}
	void rewind(asio::any_completion_handler<void(riwo::error_code)> handler)
	{
		std::move(handler)(riwo::error_code{});
	}
	void cancel() noexcept {}
};

struct source_without_cancel
{
	[[nodiscard]] riwo::optional<uint64_t> size() const noexcept { return {}; }
	[[nodiscard]] bool replayable() const noexcept { return false; }
	void read_some(riwo::mutable_buffer,
		asio::any_completion_handler<void(riwo::error_code, size_t)>) {}
	void rewind(asio::any_completion_handler<void(riwo::error_code)>) {}
};

struct source_with_wrong_completion
{
	[[nodiscard]] riwo::optional<uint64_t> size() const noexcept { return {}; }
	[[nodiscard]] bool replayable() const noexcept { return false; }
	int read_some(riwo::mutable_buffer,
		asio::any_completion_handler<void(riwo::error_code, size_t)>) { return 0; }
	void rewind(asio::any_completion_handler<void(riwo::error_code)>) {}
	void cancel() noexcept {}
};

static_assert(http::body_source_model<source_model>);
static_assert(!http::body_source_model<source_without_cancel>);
static_assert(!http::body_source_model<source_with_wrong_completion>);

static_assert(std::copyable<http::protocol_policy>);
static_assert(std::copyable<http::fields>);
static_assert(std::copyable<http::request_head>);
static_assert(std::copyable<http::response_head>);
static_assert(std::copyable<http::body_spec>);
static_assert(std::copyable<http::body_read_result>);
static_assert(std::is_trivially_copyable_v<http::body_read_result>);
static_assert(std::copyable<http::client_config>);
static_assert(std::copyable<http::server_config>);

static_assert(!std::copy_constructible<http::request>);
static_assert(std::is_nothrow_move_constructible_v<http::request>);
static_assert(!std::copy_constructible<http::client>);
static_assert(std::is_nothrow_move_constructible_v<http::client>);
static_assert(!std::copy_constructible<http::client_exchange>);
static_assert(std::is_nothrow_move_constructible_v<http::client_exchange>);
static_assert(!std::copy_constructible<http::reply>);
static_assert(std::is_nothrow_move_constructible_v<http::reply>);
static_assert(!std::copy_constructible<http::server_exchange>);
static_assert(!std::move_constructible<http::server_exchange>);
static_assert(!std::copy_constructible<http::server>);
static_assert(std::is_nothrow_move_constructible_v<http::server>);
static_assert(!std::copy_constructible<http::acceptor_wrap>);
static_assert(std::is_nothrow_move_constructible_v<http::acceptor_wrap>);
static_assert(!std::copy_constructible<http::body_reader>);
static_assert(!std::move_constructible<http::body_reader>);
static_assert(!std::copy_constructible<http::body_writer>);
static_assert(!std::move_constructible<http::body_writer>);

static_assert(std::constructible_from<native_body_reader,riwo::io_context_t&>);
static_assert(std::constructible_from<native_body_writer,riwo::io_context_t&>);
static_assert(std::constructible_from<native_client_exchange,riwo::io_context_t&>);
static_assert(std::constructible_from<native_reply,riwo::io_context_t&>);
static_assert(std::constructible_from<native_server_exchange,riwo::io_context_t&>);
static_assert(std::constructible_from<native_acceptor_wrap,
	native_acceptor_wrap::acceptor_type&&>);
static_assert(std::constructible_from<native_server,native_acceptor_wrap&&>);

static_assert(std::same_as<http::client::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::client_exchange::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::reply::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::body_reader::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::body_writer::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::server::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::acceptor_wrap::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::server::context_t,
	http::server_exchange>);
static_assert(std::same_as<http::server_exchange::request_type,
	http::request_head>);
static_assert(std::same_as<native_body_reader::executor_type,native_executor>);
static_assert(std::same_as<native_body_writer::executor_type,native_executor>);
static_assert(std::same_as<native_client_exchange::executor_type,native_executor>);
static_assert(std::same_as<native_reply::executor_type,native_executor>);
static_assert(std::same_as<native_server_exchange::executor_type,native_executor>);
static_assert(std::same_as<native_client::reply_type,native_reply>);
static_assert(std::same_as<native_client::exchange_type,native_client_exchange>);
static_assert(std::same_as<native_server::context_t,native_server_exchange>);
static_assert(std::same_as<native_server::service_executor_type,native_executor>);
static_assert(std::same_as<split_server::context_t,
	http::basic_server_exchange<native_strand>>);
static_assert(noexcept(std::declval<const http::client &>().get_executor()));
static_assert(noexcept(std::declval<const http::reply &>().get_executor()));
static_assert(noexcept(std::declval<http::client_exchange &>().cancel()));
static_assert(noexcept(std::declval<const http::body_reader &>().is_finished()));
static_assert(noexcept(std::declval<http::body_writer &>().cancel()));

riwo::awaitable<void> server_handler(http::server_exchange&) {
	co_return;
}

riwo::awaitable<void> streaming_server_handler(http::server_exchange &exchange)
{
	http::response_head head{http::status::ok};
	head.headers.set("content-type", "application/x-ndjson");
	co_await exchange.respond(std::move(head), http::body_spec::streaming(),
		riwo::use_awaitable);

	constexpr std::string_view event = "{\"ready\":true}\n";
	co_await exchange.response_body().write(riwo::buffer(event),
		riwo::use_awaitable);
	co_await exchange.response_body().finish({}, riwo::use_awaitable);
}

static_assert(http::concepts::request_handler<decltype(server_handler)>);
static_assert(http::concepts::request_handler<decltype(streaming_server_handler)>);

riwo::awaitable<void> native_server_handler(native_server_exchange&) {
	co_return;
}

static_assert(http::concepts::request_handler<
	decltype(native_server_handler),native_executor>);

struct fake_connection final : http::connection
{
	explicit fake_connection(executor_type executor) : executor_(std::move(executor)) {}
	executor_type get_executor() const noexcept override { return executor_; }
	void read_some(riwo::mutable_buffer, io_handler handler) override {
		std::move(handler)(asio::error::make_error_code(asio::error::eof), 0);
	}
	void write(riwo::const_buffer buffer, io_handler handler) override {
		std::move(handler)(riwo::error_code{}, buffer.size());
	}
	void close(close_handler handler) override {
		std::move(handler)(riwo::error_code{});
	}
	void cancel() noexcept override {}
	executor_type executor_;
};

struct fake_stream final : http::detail::stream
{
	http::protocol selected_protocol() const noexcept override {
		return http::protocol::v1_1;
	}
	void cancel() noexcept override {}
};

struct fake_session final : http::detail::client_session
{
	explicit fake_session(executor_type executor) : executor_(std::move(executor)) {}
	executor_type get_executor() const noexcept override { return executor_; }
	http::protocol selected_protocol() const noexcept override {
		return http::protocol::v1_1;
	}
	size_t available_stream_capacity() const noexcept override { return 1; }
	bool draining() const noexcept override { return false; }
	bool closed() const noexcept override { return false; }
	void open(http::request_head, http::body_spec,
		open_handler handler) override
	{
		std::move(handler)(riwo::error_code{}, std::make_shared<fake_stream>());
	}
	void graceful_shutdown(shutdown_handler handler) override {
		std::move(handler)(riwo::error_code{});
	}
	executor_type executor_;
};

void semantic_value_contracts()
{
	http::method get("GET");
	RIWO_TEST_CHECK(get.known() == http::method::get);
	RIWO_TEST_CHECK(get.is_safe());
	RIWO_TEST_CHECK(get.is_idempotent());

	http::method custom("get", {.safe = true});
	RIWO_TEST_CHECK(!custom.known());
	RIWO_TEST_CHECK(custom.is_safe());
	RIWO_TEST_CHECK(custom.is_idempotent());
	RIWO_TEST_CHECK_THROWS(http::method("GET", {}), std::invalid_argument);
	RIWO_TEST_CHECK_THROWS(http::method("bad method"), std::invalid_argument);
	RIWO_TEST_CHECK_THROWS(http::status(99), std::invalid_argument);
	RIWO_TEST_CHECK_EQ(http::status(599).code(), 599);

	http::fields fields;
	fields.add("Set-Cookie", "one=1");
	fields.add("set-cookie", "two=2");
	fields.add("Content-Type", "text/plain");
	RIWO_TEST_CHECK_EQ(fields.size(), 3U);
	RIWO_TEST_CHECK_EQ(fields.find_first("SET-COOKIE").value(), "one=1");
	RIWO_TEST_CHECK_EQ(static_cast<size_t>(std::ranges::distance(
		fields.find_all("set-cookie"))), 2U);
	fields.set("SET-cookie", "three=3");
	RIWO_TEST_CHECK_EQ(fields.size(), 2U);
	RIWO_TEST_CHECK_EQ(fields.begin()->name, "SET-cookie");
	RIWO_TEST_CHECK_THROWS(fields.add("bad name", "value"), std::invalid_argument);
	RIWO_TEST_CHECK_THROWS(fields.add("name", "bad\rvalue"), std::invalid_argument);

	auto known = http::body_spec::known(7, {{"Digest"}});
	RIWO_TEST_CHECK(known.type() == http::body_spec::mode::known_length);
	RIWO_TEST_CHECK_EQ(known.length().value(), 7U);
	RIWO_TEST_CHECK_THROWS(http::body_spec::empty().trailers().names.at(0),
		std::out_of_range);
	RIWO_TEST_CHECK_THROWS(http::body_spec::known(0, {{"Digest", "digest"}}),
		std::invalid_argument);
	RIWO_TEST_CHECK_THROWS(http::body_spec::streaming({{"Content-Length"}}),
		std::invalid_argument);

	http::request request(http::method::post, "http://example.test/resource");
	request.headers().set("content-type", "text/plain");
	request.body("abc");
	RIWO_TEST_CHECK_EQ(request.body_description().length().value(), 3U);
	http::request custom_source(http::method::put, "http://example.test/resource");
	custom_source.source(source_model{});
	RIWO_TEST_CHECK_EQ(custom_source.body_description().length().value(), 3U);
}

void fake_boundaries_and_unsupported_entry_points()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	native_body_reader reader(context);
	native_body_writer writer(context);
	native_client_exchange exchange(context);
	native_reply standalone_reply(context);
	native_server_exchange server_exchange(context);
	http::client_config config;
	config.pool.max_sessions_per_origin = 0;
	native_client native(context, config);
	native_acceptor_wrap acceptor_wrap{
		native_acceptor_wrap::acceptor_type(context)};
	native_server native_service(std::move(acceptor_wrap));
	native_service.on_default(native_server_handler);
	native_acceptor_wrap split_acceptor_wrap{
		native_acceptor_wrap::acceptor_type(context)};
	auto strand = asio::make_strand(context);
	split_server split_service(std::move(split_acceptor_wrap), strand);
	RIWO_TEST_CHECK(reader.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(writer.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(exchange.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(standalone_reply.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(server_exchange.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(native.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(native_service.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(native_service.get_service_executor() == context.get_executor());
	RIWO_TEST_CHECK(split_service.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(split_service.get_service_executor() == strand);
	fake_connection transport(executor);
	fake_session session(executor);
	RIWO_TEST_CHECK(transport.get_executor() == executor);
	RIWO_TEST_CHECK_EQ(session.available_stream_capacity(), 1U);

	http::client client(executor, config);
	RIWO_TEST_CHECK(client.get_executor() == executor);
	bool completed = false;
	client.send(http::request(http::method::get, "https://example.test/"),
		[&](riwo::error_code error, http::reply reply) {
			RIWO_TEST_CHECK_EQ(error,
				http::make_error_code(http::errc::operation_not_permitted));
			RIWO_TEST_CHECK(reply.get_executor() == executor);
			completed = true;
		});
	bool native_completed = false;
	native.send(http::request(http::method::get, "https://example.test/"),
		[&](riwo::error_code error, native_reply reply) {
			RIWO_TEST_CHECK_EQ(error,
				http::make_error_code(http::errc::operation_not_permitted));
			RIWO_TEST_CHECK(reply.get_executor() == context.get_executor());
			native_completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK(native_completed);
}

void server_facade_contract()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));

	RIWO_TEST_CHECK(service.get_executor() == executor);
	RIWO_TEST_CHECK(service.get_service_executor() == executor);

	service
		.on_request<http::method::get>("/events", streaming_server_handler)
		.on_request(http::method("SUBSCRIBE"), "/custom-events", server_handler)
		.on_default(server_handler)
		.on_server_error([](riwo::error_code) { return true; })
		.on_service_error([](http::server_exchange&,
			const std::exception&) { return true; });

	riwo::error_code error;
	service.bind({riwo::ip_type::loopback, 0}, error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK(service.acceptor().is_open());
	RIWO_TEST_CHECK(service.acceptor_wrap().acceptor().is_open());

	service.start(error);
	RIWO_TEST_CHECK(!error);

	service
		.unbound_request("/events")
		.unbound_request("/custom-events")
		.unbound_server_error()
		.unbound_service_error()
		.stop();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"semantic value contracts", semantic_value_contracts},
		{"fake boundaries and unsupported entry points",
			fake_boundaries_and_unsupported_entry_points},
		{"server facade contract", server_facade_contract}
	});
}
