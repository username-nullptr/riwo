// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/detail/connection.h>

namespace
{

namespace http = riwo::http_exp;

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
static_assert(std::copyable<http::response_head>);
static_assert(std::copyable<http::body_spec>);
static_assert(std::copyable<http::client_config>);
static_assert(std::copyable<http::server_config>);

static_assert(!std::copy_constructible<http::request>);
static_assert(std::is_nothrow_move_constructible_v<http::request>);
static_assert(!std::copy_constructible<http::client>);
static_assert(std::is_nothrow_move_constructible_v<http::client>);
static_assert(!std::copy_constructible<http::client_exchange>);
static_assert(std::is_nothrow_move_constructible_v<http::client_exchange>);
static_assert(!std::copy_constructible<http::response>);
static_assert(std::is_nothrow_move_constructible_v<http::response>);
static_assert(!std::copy_constructible<http::server_exchange>);
static_assert(!std::move_constructible<http::server_exchange>);
static_assert(!std::copy_constructible<http::body_reader>);
static_assert(!std::move_constructible<http::body_reader>);
static_assert(!std::copy_constructible<http::body_writer>);
static_assert(!std::move_constructible<http::body_writer>);

static_assert(std::same_as<http::client::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::client_exchange::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::response::executor_type, asio::any_io_executor>);
static_assert(std::same_as<http::body_reader::executor_type,
	asio::any_io_executor>);
static_assert(std::same_as<http::body_writer::executor_type,
	asio::any_io_executor>);
static_assert(noexcept(std::declval<const http::client &>().get_executor()));
static_assert(noexcept(std::declval<const http::response &>().get_executor()));
static_assert(noexcept(std::declval<http::client_exchange &>().cancel()));
static_assert(noexcept(std::declval<http::body_writer &>().cancel()));

struct fake_connection final : http::detail::connection
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
	void open(http::detail::request_head, http::body_spec,
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
	request.body(std::string("abc"));
	RIWO_TEST_CHECK_EQ(request.body_description().length().value(), 3U);
	http::request custom_source(http::method::put, "http://example.test/resource");
	custom_source.source(source_model{});
	RIWO_TEST_CHECK_EQ(custom_source.body_description().length().value(), 3U);
}

void fake_boundaries_and_unsupported_entry_points()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	fake_connection transport(executor);
	fake_session session(executor);
	RIWO_TEST_CHECK(transport.get_executor() == executor);
	RIWO_TEST_CHECK_EQ(session.available_stream_capacity(), 1U);

	http::client client(executor);
	RIWO_TEST_CHECK(client.get_executor() == executor);
	bool completed = false;
	client.send(http::request(http::method::get, "http://example.test/"),
		[&](riwo::error_code error, http::response response) {
			RIWO_TEST_CHECK_EQ(error,
				http::make_error_code(http::errc::operation_not_supported));
			RIWO_TEST_CHECK(response.get_executor() == asio::any_io_executor{});
			completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(completed);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"semantic value contracts", semantic_value_contracts},
		{"fake boundaries and unsupported entry points",
			fake_boundaries_and_unsupported_entry_points}
	});
}
