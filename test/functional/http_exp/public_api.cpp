// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/client/detail/session_pool.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/protocol/v1/detail/session.h>
#include <riwo/http_exp/transport/connection.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace
{

namespace http = riwo::http_exp;

using native_executor = riwo::io_context_t::executor_type;
using native_body_reader = http::basic_body_reader<native_executor>;
using native_body_writer = http::basic_body_writer<native_executor>;
using native_client_exchange = http::basic_client_exchange<native_executor>;
using native_reply = http::basic_reply<native_executor>;
using native_server_exchange = http::basic_server_exchange<native_executor>;
using native_duplex_channel = http::basic_duplex_channel<native_executor>;
using native_client = http::basic_client<native_executor>;
using native_server = http::basic_server<native_executor>;
using native_acceptor_wrap = http::basic_acceptor_wrap<native_executor>;
using native_strand = asio::strand<native_executor>;
using split_server = http::basic_server<native_executor,native_strand>;

static_assert(std::same_as<http::body_reader,http::basic_body_reader<>>);
static_assert(std::same_as<http::body_writer,http::basic_body_writer<>>);
static_assert(std::same_as<http::client,http::basic_client<>>);
static_assert(std::same_as<http::client_exchange,http::basic_client_exchange<>>);
static_assert(std::same_as<http::reply,http::basic_reply<>>);
static_assert(std::same_as<http::server_exchange,http::basic_server_exchange<>>);
static_assert(std::same_as<http::duplex_channel,http::basic_duplex_channel<>>);
static_assert(std::same_as<http::byte_stream,http::basic_byte_stream<>>);
static_assert(std::same_as<http::negotiated_transport,
	http::basic_negotiated_transport<>>);
static_assert(std::same_as<http::transport_connector,
	http::basic_transport_connector<>>);
static_assert(std::same_as<http::acceptor_wrap,http::basic_acceptor_wrap<>>);
static_assert(std::same_as<http::middleware,http::basic_middleware<>>);
static_assert(std::same_as<http::controller,http::basic_controller<>>);
static_assert(std::same_as<http::aop,http::middleware>);
static_assert(std::same_as<http::ctrlr_aop,http::controller>);
static_assert(std::same_as<http::basic_tcp_server<>,http::basic_server<>>);
static_assert(std::same_as<http::tcp_server,http::basic_tcp_server<>>);
static_assert(std::same_as<http::server,http::tcp_server>);

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

static_assert(http::concepts::body_source_model<source_model>);
static_assert(!http::concepts::body_source_model<source_without_cancel>);
static_assert(!http::concepts::body_source_model<source_with_wrong_completion>);

static_assert(std::copyable<http::protocol_policy>);
static_assert(std::same_as<decltype(http::field::value),riwo::value>);
static_assert(std::copyable<http::fields>);
static_assert(std::copyable<http::request_head>);
static_assert(std::copyable<http::response_head>);
static_assert(std::same_as<http::reply_head,http::response_head>);
static_assert(std::is_aggregate_v<http::request_head>);
static_assert(std::is_aggregate_v<http::response_head>);
static_assert(std::same_as<decltype(http::request_head::headers), http::fields>);
static_assert(std::same_as<decltype(http::response_head::headers), http::fields>);
static_assert(std::copyable<http::cookie_pair>);
static_assert(std::copyable<http::response_cookie>);
static_assert(std::copyable<http::cookie_jar_config>);
static_assert(std::copyable<http::body_spec>);
static_assert(std::copyable<http::body_read_result>);
static_assert(std::is_trivially_copyable_v<http::body_read_result>);
static_assert(std::copyable<http::client_config>);
static_assert(std::copyable<http::server_config>);
static_assert(std::copyable<http::proxy_policy>);
static_assert(std::copyable<http::request_options>);
static_assert(std::copyable<http::parameter>);
static_assert(std::copyable<http::parameter_view>);
static_assert(std::copyable<http::route_pattern>);
static_assert(std::copyable<http::application_session_config>);
static_assert(std::constructible_from<http::application_session_manager>);
static_assert(std::derived_from<http::aop,http::middleware>);
#if RIWO_OPENSSL_SUPPORT
static_assert(std::constructible_from<http::server_tls_context,
	http::server_tls_config>);
static_assert(std::same_as<decltype(http::make_tls_acceptor(
	std::declval<http::acceptor_wrap>(),
	std::declval<std::shared_ptr<http::server_tls_context>>())),
	http::acceptor_wrap>);
static_assert(std::constructible_from<http::server,
	std::shared_ptr<http::server_tls_context>>);
static_assert(std::constructible_from<http::server,
	std::shared_ptr<http::server_tls_context>,http::server_config>);
#endif

static_assert(!std::copy_constructible<http::request>);
static_assert(!std::copy_constructible<http::cookie_jar>);
static_assert(std::is_nothrow_move_constructible_v<http::request>);
static_assert(!std::copy_constructible<http::client>);
static_assert(std::is_nothrow_move_constructible_v<http::client>);
static_assert(std::constructible_from<http::client>);
static_assert(std::constructible_from<http::client,http::client_config>);
static_assert(std::constructible_from<native_client>);
static_assert(std::constructible_from<http::body_reader>);
static_assert(std::constructible_from<http::body_writer>);
static_assert(std::constructible_from<http::client_exchange>);
static_assert(std::constructible_from<http::reply>);
static_assert(std::constructible_from<http::server_exchange>);
static_assert(requires(http::server_exchange &exchange,
	std::filesystem::path path, std::fstream &stream, std::FILE *file,
	riwo::error_code &error) {
	{ exchange.save_file(std::move(path), error) } -> std::same_as<uint64_t>;
	{ exchange.save_file(http::borrowed_file(stream), error) } ->
		std::same_as<uint64_t>;
	{ exchange.save_file(http::owned_file(file), error) } ->
		std::same_as<uint64_t>;
	exchange.respond_file(path, riwo::use_awaitable);
	exchange.respond_file(path, http::file_response_options{},
		riwo::use_awaitable);
});
static_assert(std::constructible_from<http::duplex_channel>);
static_assert(std::constructible_from<http::acceptor_wrap>);
static_assert(std::constructible_from<http::server>);
static_assert(std::constructible_from<http::server,http::server_config>);
static_assert(std::constructible_from<http::server,asio::any_io_executor>);
static_assert(std::constructible_from<http::server,riwo::io_context_t&>);
static_assert(std::constructible_from<http::tcp_connection>);
static_assert(std::constructible_from<native_body_reader>);
static_assert(std::constructible_from<native_body_writer>);
static_assert(std::constructible_from<native_client_exchange>);
static_assert(std::constructible_from<native_reply>);
static_assert(std::constructible_from<native_server_exchange>);
static_assert(std::constructible_from<native_duplex_channel>);
static_assert(std::constructible_from<native_acceptor_wrap>);
static_assert(std::constructible_from<native_server>);
static_assert(std::constructible_from<native_server,native_executor>);
static_assert(std::constructible_from<native_server,riwo::io_context_t&>);
static_assert(std::constructible_from<http::body_reader,
	std::shared_ptr<http::detail::body_reader_state>>);
static_assert(std::constructible_from<http::body_writer,
	std::shared_ptr<http::detail::body_writer_state>>);
static_assert(requires(http::server_exchange &exchange,
	http::body_spec spec, riwo::const_buffer body, source_model source) {
	exchange.respond(std::move(spec), riwo::use_awaitable);
	exchange.respond(body, riwo::use_awaitable);
	exchange.respond("body", riwo::use_awaitable);
	exchange.respond(std::move(source), riwo::use_awaitable);
});
static_assert(std::constructible_from<http::client_exchange,
	std::shared_ptr<http::detail::stream>>);
static_assert(std::constructible_from<http::reply,http::protocol,
	http::response_head,std::shared_ptr<http::detail::body_reader_state>>);
static_assert(std::constructible_from<http::server_exchange,
	std::shared_ptr<http::detail::stream>,http::request_head>);
static_assert(std::constructible_from<http::duplex_channel,
	std::shared_ptr<http::detail::duplex_state>>);
static_assert(std::constructible_from<http::client,
	std::shared_ptr<http::detail::client_session>>);
static_assert(std::constructible_from<http::client,
	std::shared_ptr<http::transport_connector>>);
static_assert(std::constructible_from<http::client,
	std::shared_ptr<http::detail::client_session>,http::client_config>);
static_assert(std::constructible_from<http::client,
	std::shared_ptr<http::transport_connector>,http::client_config>);
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
	native_acceptor_wrap::acceptor_t&&>);
static_assert(std::constructible_from<native_server,native_acceptor_wrap&&>);
static_assert(std::constructible_from<native_server,native_acceptor_wrap&&,
	http::server_config>);
static_assert(std::same_as<decltype(std::declval<const http::client&>().config()),
	http::client_config>);
static_assert(std::same_as<decltype(std::declval<http::client&>().set_config(
	std::declval<http::client_config>())),http::client&>);

static_assert(std::same_as<http::client::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::client::executor_t, asio::any_io_executor>);
static_assert(std::same_as<http::client::config_t, http::client_config>);
static_assert(std::same_as<http::client::reply_t, http::reply>);
static_assert(std::same_as<http::client::exchange_t, http::client_exchange>);
static_assert(std::same_as<http::client_exchange::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::client_exchange::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::client_exchange::body_reader_t,
	http::body_reader>);
static_assert(std::same_as<http::client_exchange::body_writer_t,
	http::body_writer>);
static_assert(std::same_as<http::reply::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::reply::executor_t, asio::any_io_executor>);
static_assert(std::same_as<http::reply::body_reader_t, http::body_reader>);
static_assert(std::same_as<http::body_reader::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::body_reader::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::body_writer::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::body_writer::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::server::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::server::executor_t, asio::any_io_executor>);
static_assert(std::same_as<http::server::service_executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::server::config_t, http::server_config>);
static_assert(std::same_as<http::acceptor_wrap::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::acceptor_wrap::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::acceptor_wrap::protocol_t, asio::ip::tcp>);
static_assert(std::same_as<http::acceptor_wrap::acceptor_t,
	asio::ip::tcp::acceptor>);
static_assert(std::same_as<http::server::context_t,
	http::server_exchange>);
static_assert(std::same_as<http::server_exchange::request_t,
	http::request_head>);
static_assert(std::same_as<http::server_exchange::body_reader_t,
	http::body_reader>);
static_assert(std::same_as<http::server_exchange::body_writer_t,
	http::body_writer>);
static_assert(std::same_as<http::duplex_channel::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::duplex_channel::endpoint_t,
	asio::ip::tcp::endpoint>);
static_assert(std::same_as<http::connection::endpoint_t,
	asio::ip::tcp::endpoint>);
static_assert(std::same_as<http::connection::timeout_t,
	std::chrono::milliseconds>);
static_assert(std::same_as<http::transport_connector::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::transport_connector::result_t,
	http::negotiated_transport>);
static_assert(std::same_as<http::acceptor_wrap::transport_t,
	http::negotiated_transport>);
static_assert(std::same_as<http::byte_stream::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::detail::connector::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::detail::session_pool::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::detail::client_session::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::detail::server_session::executor_t,
	asio::any_io_executor>);
static_assert(std::same_as<http::fields::container_t,
	std::vector<http::field>>);
static_assert(std::same_as<http::field_range::const_iterator::value_type,
	http::field>);
static_assert(std::same_as<http::field_range::const_iterator::difference_type,
	std::ptrdiff_t>);
static_assert(std::same_as<native_body_reader::executor_type,native_executor>);
static_assert(std::same_as<native_body_reader::executor_t,native_executor>);
static_assert(std::same_as<native_body_writer::executor_type,native_executor>);
static_assert(std::same_as<native_client_exchange::executor_type,native_executor>);
static_assert(std::same_as<native_reply::executor_type,native_executor>);
static_assert(std::same_as<native_server_exchange::executor_type,native_executor>);
static_assert(std::same_as<native_client::reply_t,native_reply>);
static_assert(std::same_as<native_client::exchange_t,native_client_exchange>);
static_assert(std::same_as<native_server::context_t,native_server_exchange>);
static_assert(std::same_as<native_server::service_executor_t,native_executor>);
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
	void read_some(riwo::mutable_buffer, io_handler_t handler) override {
		std::move(handler)(asio::error::make_error_code(asio::error::eof), 0);
	}
	void write(riwo::const_buffer buffer, io_handler_t handler) override {
		std::move(handler)(riwo::error_code{}, buffer.size());
	}
	void close(close_handler_t handler) override {
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
	http::stream_capabilities capabilities() const noexcept override {
		return {};
	}
	http::request_head request() const override { return {}; }
	std::shared_ptr<http::detail::body_reader_state>
	inbound_body() const noexcept override {
		return http::detail::make_body_reader_state();
	}
	std::shared_ptr<http::detail::body_writer_state>
	outbound_body() const noexcept override {
		return http::detail::make_body_writer_state();
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
		open_handler_t handler) override
	{
		std::move(handler)(riwo::error_code{}, std::make_shared<fake_stream>());
	}
	void graceful_shutdown(shutdown_handler_t handler) override {
		std::move(handler)(riwo::error_code{});
	}
	executor_type executor_;
};

struct fake_transport_connector final : http::transport_connector
{
	explicit fake_transport_connector(executor_type executor) :
		executor_(std::move(executor)) {}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}

	void connect(http::connect_request, connect_handler_t handler) override {
		std::move(handler)(
			http::make_error_code(http::errc::operation_not_supported),
			result_t{});
	}

	void cancel() noexcept override {}

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
	RIWO_TEST_CHECK(http::status(http::status::ok) == http::status::ok);
	RIWO_TEST_CHECK(http::status::ok == http::status(http::status::ok));
	RIWO_TEST_CHECK(http::status(http::status::ok) != http::status::created);
	RIWO_TEST_CHECK_EQ(http::status(http::status::reset_content).text(),
		"Reset Content");
	RIWO_TEST_CHECK_EQ(http::status(418).text(), "I'm a teapot");
	RIWO_TEST_CHECK(http::status(http::status::ok).allows_body());
	RIWO_TEST_CHECK(!http::status(http::status::no_content).allows_body());
	RIWO_TEST_CHECK(!http::status(http::status::reset_content).allows_body());
	RIWO_TEST_CHECK(!http::status(http::status::not_modified).allows_body());

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

	http::fields copied_fields = fields;
	copied_fields.add("X-Copy", "copied");
	copied_fields.add("Set-Cookie", "four=4");
	RIWO_TEST_CHECK(!fields.find_first("x-copy"));
	RIWO_TEST_CHECK_EQ(copied_fields.find_first("x-copy").value(), "copied");

	auto cookie_range = copied_fields.find_all("set-cookie");
	auto cookie_cursor = cookie_range.begin();
	auto next_cookie_cursor = cookie_cursor;
	++next_cookie_cursor;
	RIWO_TEST_CHECK_EQ(cookie_cursor->value.to_string(), "three=3");
	RIWO_TEST_CHECK_EQ(next_cookie_cursor->value.to_string(), "four=4");

	http::fields typed_fields;
	typed_fields.add("Content-Length", 42U);
	typed_fields.set("X-Enabled", true);
	RIWO_TEST_CHECK_EQ(typed_fields.find_first("content-length").value(), "42");
	RIWO_TEST_CHECK_EQ(typed_fields.begin()->value.get<uint32_t>().value(), 42U);
	RIWO_TEST_CHECK_EQ(std::next(typed_fields.begin())->value.to_bool().value(), true);

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

void head_convenience_contracts()
{
	http::request_head request {
		.method = http::method::post,
		.url = riwo::url("https://example.test/upload?q=1"),
		.scheme = "https",
		.authority = "example.test:8443",
		.target = "/upload?q=1"
	};
	request.set_header("Content-Type", "text/plain");

	RIWO_TEST_CHECK_EQ(request.header("content-type").value(), "text/plain");
	RIWO_TEST_CHECK(request.has_header("CONTENT-TYPE"));
	RIWO_TEST_CHECK(request.contains_header("Content-Type"));
	RIWO_TEST_CHECK_EQ(request.content_type().value(), "text/plain");
	RIWO_TEST_CHECK(!request.content_length());
	RIWO_TEST_CHECK_EQ(request.host(), "example.test:8443");
	RIWO_TEST_CHECK_EQ(request.path(), "/upload?q=1");
	RIWO_TEST_CHECK(request.is_https());
	RIWO_TEST_CHECK(!request.is_http());
	RIWO_TEST_CHECK(!request.is_upgrade());

	request
		.set_header("Content-Length", 12U)
		.add_header("X-Trace", "one")
		.add_header("X-Trace", "two");
	RIWO_TEST_CHECK_EQ(request.content_length().value(), 12U);
	RIWO_TEST_CHECK_EQ(std::ranges::distance(
		request.headers.find_all("x-trace")), 2);

	request.upgrade_protocol = "websocket";
	RIWO_TEST_CHECK(request.is_upgrade());
	request.unset_header("X-Trace").erase_header("content-length");
	RIWO_TEST_CHECK(!request.has_header("X-Trace"));
	RIWO_TEST_CHECK(!request.content_length());

	http::request_head submitted {
		.url = riwo::url("http://fallback.test/resource")
	};
	RIWO_TEST_CHECK(submitted.is_http());
	RIWO_TEST_CHECK_EQ(submitted.host(), "fallback.test");
	RIWO_TEST_CHECK_EQ(submitted.path(), "/");

	http::response_head response {http::status::created};
	response
		.set_header("Content-Type", "application/json")
		.set_header("Content-Length", 2U)
		.set_header("Location", "/resource/1");
	RIWO_TEST_CHECK_EQ(response.header("content-type").value(),
		"application/json");
	RIWO_TEST_CHECK_EQ(response.content_type().value(), "application/json");
	RIWO_TEST_CHECK_EQ(response.content_length().value(), 2U);
	RIWO_TEST_CHECK_EQ(response.location().value(), "/resource/1");
	RIWO_TEST_CHECK(response.is_success());
	RIWO_TEST_CHECK(!response.is_redirection());
	RIWO_TEST_CHECK(!response.is_error());
	RIWO_TEST_CHECK(response.allows_body());

	response.status = http::status::found;
	RIWO_TEST_CHECK(response.is_redirection());
	RIWO_TEST_CHECK(!response.is_error());
	response.status = http::status::not_found;
	RIWO_TEST_CHECK(response.is_client_error());
	RIWO_TEST_CHECK(response.is_error());
	response.status = http::status::service_unavailable;
	RIWO_TEST_CHECK(response.is_server_error());
	RIWO_TEST_CHECK(response.is_error());
	response.status = http::status::continue_upload;
	RIWO_TEST_CHECK(response.is_informational());
	RIWO_TEST_CHECK(!response.allows_body());

	response.set_header("Content-Length", "invalid");
	RIWO_TEST_CHECK(!response.content_length());
	response.unset_header("Content-Length");
	RIWO_TEST_CHECK(!response.contains_header("Content-Length"));
}

void cookie_value_contracts()
{
	http::fields request_headers;
	request_headers.add("Cookie", "sid=abc; duplicate=short");
	request_headers.add("cookie", "duplicate=long; enabled=true");
	auto parsed_request = http::parse_cookie_fields(request_headers);
	RIWO_TEST_CHECK(parsed_request);
	RIWO_TEST_CHECK_EQ(parsed_request->size(), 4U);
	RIWO_TEST_CHECK_EQ(parsed_request->at(0).name, "sid");
	RIWO_TEST_CHECK_EQ(parsed_request->at(0).value.to_string(), "abc");
	RIWO_TEST_CHECK_EQ(parsed_request->at(1).name, "duplicate");
	RIWO_TEST_CHECK_EQ(parsed_request->at(2).name, "duplicate");
	RIWO_TEST_CHECK_EQ(http::serialize_cookie(*parsed_request),
		"sid=abc; duplicate=short; duplicate=long; enabled=true");

	http::fields invalid_headers;
	invalid_headers.add("Cookie", "valid=one; missing-value");
	auto invalid = http::parse_cookie_fields(invalid_headers);
	RIWO_TEST_CHECK(!invalid);
	RIWO_TEST_CHECK_EQ(invalid.error(),
		http::make_error_code(http::errc::invalid_cookie));

	http::response_cookie response {
		.name = "sid",
		.value = riwo::value("42"),
		.path = "/",
		.max_age = std::chrono::seconds(60),
		.same_site = http::cookie_same_site::lax,
		.secure = true,
		.http_only = true,
		.extensions = {{"Priority", riwo::value("High")}}
	};
	RIWO_TEST_CHECK_EQ(http::serialize_set_cookie(response),
		"sid=42; Max-Age=60; Path=/; Secure; HttpOnly; SameSite=Lax; Priority=High");
	RIWO_TEST_CHECK_THROWS(http::serialize_set_cookie({
		.name = "sid", .value = riwo::value("42"), .domain = "bad domain"}),
		std::invalid_argument);
	http::fields response_headers;
	http::add_set_cookie(response_headers, response);
	RIWO_TEST_CHECK_EQ(response_headers.find_first("set-cookie").value(),
		"sid=42; Max-Age=60; Path=/; Secure; HttpOnly; SameSite=Lax; Priority=High");

	auto parsed_response = http::parse_set_cookie(
		"sid=42; Path=/account; Secure; HttpOnly; SameSite=Strict; Feature=on");
	RIWO_TEST_CHECK(parsed_response);
	RIWO_TEST_CHECK_EQ(parsed_response->name, "sid");
	RIWO_TEST_CHECK_EQ(parsed_response->value.to_string(), "42");
	RIWO_TEST_CHECK_EQ(parsed_response->path.value(), "/account");
	RIWO_TEST_CHECK(parsed_response->secure);
	RIWO_TEST_CHECK(parsed_response->http_only);
	RIWO_TEST_CHECK(parsed_response->same_site == http::cookie_same_site::strict);
	RIWO_TEST_CHECK_EQ(parsed_response->extensions.size(), 1U);

	http::cookie_jar jar;
	const riwo::url origin("https://api.example.test/account/login");
	RIWO_TEST_CHECK(jar.store(origin, {
		.name = "sid", .value = riwo::value("host"),
		.path = "/account", .secure = true}));
	RIWO_TEST_CHECK(jar.store(origin, {
		.name = "sid", .value = riwo::value("domain"),
		.domain = "example.test", .path = "/"}));
	RIWO_TEST_CHECK(jar.store(origin, {
		.name = "strict", .value = riwo::value("one"),
		.path = "/", .same_site = http::cookie_same_site::strict}));
	RIWO_TEST_CHECK(jar.store(origin, {
		.name = "cross", .value = riwo::value("one"),
		.path = "/", .same_site = http::cookie_same_site::none,
		.secure = true}));
	RIWO_TEST_CHECK(!jar.store(riwo::url("http://api.example.test/"), {
		.name = "secure", .value = riwo::value("bad"), .secure = true}));
	RIWO_TEST_CHECK(!jar.store(origin, {
		.name = "__Host-invalid", .value = riwo::value("bad"),
		.domain = "example.test", .path = "/", .secure = true}));
	RIWO_TEST_CHECK(!jar.store(riwo::url("http://api.example.test/account/x"), {
		.name = "sid", .value = riwo::value("shadow"),
		.path = "/account/x", .max_age = std::chrono::seconds(0)}));

	auto account = jar.select(
		riwo::url("https://api.example.test/account/profile"));
	RIWO_TEST_CHECK_EQ(account.size(), 4U);
	RIWO_TEST_CHECK_EQ(account.at(0).value.to_string(), "host");
	RIWO_TEST_CHECK_EQ(account.at(1).value.to_string(), "domain");

	http::cookie_request_context cross_site;
	cross_site.site = http::cookie_site_context::cross_site;
	cross_site.request_method = http::method(http::method::post);
	auto cross = jar.select(
		riwo::url("https://api.example.test/account/profile"), cross_site);
	RIWO_TEST_CHECK_EQ(cross.size(), 1U);
	RIWO_TEST_CHECK_EQ(cross.front().name, "cross");
	RIWO_TEST_CHECK(std::ranges::none_of(cross, [](const http::cookie_pair &item) {
		return item.name == "strict";
	}));
	cross_site.http_api = false;
	cross_site.top_level_navigation = true;
	cross_site.request_method = http::method(http::method::get);
	RIWO_TEST_CHECK_EQ(jar.select(
		riwo::url("https://api.example.test/account/profile"), cross_site).size(), 1U);

	RIWO_TEST_CHECK(jar.store(origin, {
		.name = "sid", .value = riwo::value("gone"),
		.path = "/account", .max_age = std::chrono::seconds(0),
		.secure = true}));
	auto after_delete = jar.select(
		riwo::url("https://api.example.test/account/profile"));
	RIWO_TEST_CHECK_EQ(after_delete.size(), 3U);
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
	http::client default_client;
	http::client configured_client(config);
	auto updated_client_config = default_client.config();
	updated_client_config.pool.max_sessions_per_origin = 7;
	RIWO_TEST_CHECK(&default_client.set_config(updated_client_config) ==
		&default_client);
	RIWO_TEST_CHECK_EQ(default_client.config().pool.max_sessions_per_origin, 7U);
	http::body_reader default_reader;
	http::body_writer default_writer;
	http::client_exchange default_client_exchange;
	http::reply default_reply;
	http::server_exchange default_server_exchange;
	http::duplex_channel default_channel;
	http::acceptor_wrap default_acceptor_wrap;
	http::server default_server;
	http::tcp_connection default_transport;
	auto global_executor = asio::any_io_executor(riwo::get_executor());
	auto reader_state = http::detail::make_body_reader_state();
	auto writer_state = http::detail::make_body_writer_state();
	auto stream = std::make_shared<fake_stream>();
	http::body_reader reader_with_state(reader_state);
	http::body_writer writer_with_state(writer_state);
	http::client_exchange exchange_with_stream(stream);
	http::reply reply_with_body(http::protocol::v1_1,
		http::reply_head{http::status::ok}, reader_state);
	http::server_exchange server_exchange_with_stream(stream,
		http::request_head{});
	std::shared_ptr<http::detail::duplex_state> duplex_state;
	http::duplex_channel channel_with_state(duplex_state);
	auto client_session = std::make_shared<fake_session>(global_executor);
	http::client session_client(client_session, config);
	auto connector = std::make_shared<fake_transport_connector>(global_executor);
	http::client connector_client(connector, config);
	auto default_connector = http::detail::make_default_connector();
	auto connector_adapter = http::detail::make_connector_adapter(
		std::static_pointer_cast<http::transport_connector>(connector));
	http::detail::session_pool pool_with_default_executor(config);
	auto client_state_with_default_executor =
		http::detail::make_client_state(config);
	auto h1_session_with_default_executor =
		http::v1::detail::make_client_session(http::protocol::v1_1);
	auto body_duplex_with_default_executor =
		http::detail::make_body_duplex_state(http::protocol::v1_1,
			reader_state, writer_state, {}, {}, {}, [] {});
	native_client native_default_client;
	native_client native(context, config);
	native_server native_server_from_context(context);
	http::server server_from_executor(executor);
	native_acceptor_wrap acceptor_wrap{
		native_acceptor_wrap::acceptor_t(context)};
	native_server native_service(std::move(acceptor_wrap));
	native_service.on_default(native_server_handler);
	native_acceptor_wrap split_acceptor_wrap{
		native_acceptor_wrap::acceptor_t(context)};
	auto strand = asio::make_strand(context);
	split_server split_service(std::move(split_acceptor_wrap), strand);
	RIWO_TEST_CHECK(reader.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(writer.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(exchange.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(standalone_reply.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(server_exchange.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(default_client.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(configured_client.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_reader.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_writer.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_client_exchange.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_reply.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_server_exchange.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_channel.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_acceptor_wrap.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_server.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_server.get_service_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(default_transport.get_executor() ==
		asio::any_io_executor(riwo::get_executor()));
	RIWO_TEST_CHECK(reader_with_state.get_executor() == global_executor);
	RIWO_TEST_CHECK(writer_with_state.get_executor() == global_executor);
	RIWO_TEST_CHECK(exchange_with_stream.get_executor() == global_executor);
	RIWO_TEST_CHECK(reply_with_body.get_executor() == global_executor);
	RIWO_TEST_CHECK(server_exchange_with_stream.get_executor() == global_executor);
	RIWO_TEST_CHECK(channel_with_state.get_executor() == global_executor);
	RIWO_TEST_CHECK(session_client.get_executor() == global_executor);
	RIWO_TEST_CHECK(connector_client.get_executor() == global_executor);
	RIWO_TEST_CHECK(default_connector->get_executor() == global_executor);
	RIWO_TEST_CHECK(connector_adapter->get_executor() == global_executor);
	RIWO_TEST_CHECK(pool_with_default_executor.get_executor() == global_executor);
	RIWO_TEST_CHECK(client_state_with_default_executor);
	RIWO_TEST_CHECK(h1_session_with_default_executor->get_executor() ==
		global_executor);
	RIWO_TEST_CHECK(body_duplex_with_default_executor->get_executor() ==
		global_executor);
	RIWO_TEST_CHECK(native_default_client.get_executor() ==
		riwo::get_executor());
	RIWO_TEST_CHECK(native.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(native_server_from_context.get_executor() ==
		context.get_executor());
	RIWO_TEST_CHECK(native_server_from_context.get_service_executor() ==
		context.get_executor());
	RIWO_TEST_CHECK(server_from_executor.get_executor() == executor);
	RIWO_TEST_CHECK(server_from_executor.get_service_executor() == executor);
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
		{"head convenience contracts", head_convenience_contracts},
		{"cookie value contracts", cookie_value_contracts},
		{"fake boundaries and unsupported entry points",
			fake_boundaries_and_unsupported_entry_points},
		{"server facade contract", server_facade_contract}
	});
}
