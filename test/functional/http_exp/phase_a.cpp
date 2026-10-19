// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <fstream>

namespace
{

namespace http = riwo::http_exp;

class fake_reader_adapter final : public http::detail::body_reader_adapter
{
public:
	explicit fake_reader_adapter(std::string bytes = {}, http::fields trailers = {}) :
		bytes_(std::move(bytes)), trailers_(std::move(trailers))
	{
	}

	size_t read_some_sync(riwo::mutable_buffer buffer,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		if( cancelled_ )
		{
			error = asio::error::make_error_code(asio::error::operation_aborted);
			return 0;
		}
		if( buffer.size() == 0 ) return 0;
		if( offset_ == bytes_.size() )
		{
			error = asio::error::make_error_code(asio::error::eof);
			return 0;
		}
		const auto size = std::min(buffer.size(), bytes_.size() - offset_);
		std::memcpy(buffer.data(), bytes_.data() + offset_, size);
		offset_ += size;
		return size;
	}

	void read_some(riwo::mutable_buffer buffer, read_handler handler) override
	{
		if( hold_ )
		{
			pending_buffer_ = buffer;
			pending_.emplace(std::move(handler));
			return;
		}
		riwo::error_code error;
		auto size = read_some_sync(buffer, error);
		std::move(handler)(error, size);
	}

	http::fields trailers_sync(riwo::error_code &error) noexcept override
	{
		error.clear();
		return trailers_;
	}

	void trailers(trailers_handler handler) override {
		std::move(handler)(riwo::error_code{}, trailers_);
	}

	void cancel() noexcept override
	{
		cancelled_ = true;
		if( pending_ )
		{
			auto handler = std::move(*pending_);
			pending_.reset();
			std::move(handler)(asio::error::make_error_code(
				asio::error::operation_aborted), 0);
		}
	}

	void hold() noexcept { hold_ = true; }
	void complete()
	{
		hold_ = false;
		auto handler = std::move(*pending_);
		pending_.reset();
		riwo::error_code error;
		auto size = read_some_sync(pending_buffer_, error);
		std::move(handler)(error, size);
	}

private:
	std::string bytes_;
	http::fields trailers_;
	size_t offset_ = 0;
	bool hold_ = false;
	bool cancelled_ = false;
	riwo::mutable_buffer pending_buffer_;
	std::optional<read_handler> pending_;
};

class fake_writer_adapter final : public http::detail::body_writer_adapter
{
public:
	size_t write_sync(riwo::const_buffer buffer,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		if( cancelled_ )
		{
			error = asio::error::make_error_code(asio::error::operation_aborted);
			return 0;
		}
		const auto *data = static_cast<const char*>(buffer.data());
		bytes_.append(data, data + buffer.size());
		return buffer.size();
	}

	void write(riwo::const_buffer buffer, write_handler handler) override
	{
		if( hold_ )
		{
			pending_buffer_ = buffer;
			pending_.emplace(std::move(handler));
			return;
		}
		riwo::error_code error;
		auto size = write_sync(buffer, error);
		std::move(handler)(error, size);
	}

	void finish_sync(http::fields trailers,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		trailers_ = std::move(trailers);
		finished_ = true;
	}

	void finish(http::fields trailers, finish_handler handler) override
	{
		riwo::error_code error;
		finish_sync(std::move(trailers), error);
		std::move(handler)(error);
	}

	void cancel() noexcept override
	{
		cancelled_ = true;
		if( pending_ )
		{
			auto handler = std::move(*pending_);
			pending_.reset();
			std::move(handler)(asio::error::make_error_code(
				asio::error::operation_aborted), 0);
		}
	}

	void hold() noexcept { hold_ = true; }
	void complete()
	{
		hold_ = false;
		auto handler = std::move(*pending_);
		pending_.reset();
		riwo::error_code error;
		auto size = write_sync(pending_buffer_, error);
		std::move(handler)(error, size);
	}

	std::string bytes_;
	http::fields trailers_;
	bool finished_ = false;
	bool hold_ = false;
	bool cancelled_ = false;
	riwo::const_buffer pending_buffer_;
	std::optional<write_handler> pending_;
};

class fake_stream final : public http::detail::stream
{
public:
	explicit fake_stream(http::body_spec spec) :
		reader_adapter(std::make_shared<fake_reader_adapter>("reply")),
		writer_adapter(std::make_shared<fake_writer_adapter>()),
		reader(http::detail::make_body_reader_state(reader_adapter)),
		writer(http::detail::make_body_writer_state(writer_adapter,
			std::move(spec), true))
	{
	}

	http::protocol selected_protocol() const noexcept override {
		return http::protocol::v1_1;
	}

	std::shared_ptr<http::detail::body_reader_state>
	inbound_body() const noexcept override { return reader; }

	std::shared_ptr<http::detail::body_writer_state>
	outbound_body() const noexcept override { return writer; }

	http::response_head wait_response_sync(
		riwo::error_code &error) noexcept override
	{
		error.clear();
		return {http::status::created, {}};
	}

	void wait_response(response_handler handler) override {
		std::move(handler)(riwo::error_code{},
			http::response_head{http::status::created, {}});
	}

	void respond_sync(http::response_head head, http::body_spec,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		response = std::move(head);
	}

	void respond(http::response_head head, http::body_spec,
		operation_handler handler) override
	{
		response = std::move(head);
		std::move(handler)(riwo::error_code{});
	}

	void inform_sync(http::response_head head,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		informationals.push_back(std::move(head));
	}

	void inform(http::response_head head, operation_handler handler) override
	{
		informationals.push_back(std::move(head));
		std::move(handler)(riwo::error_code{});
	}

	void cancel() noexcept override { cancelled = true; }

	std::shared_ptr<fake_reader_adapter> reader_adapter;
	std::shared_ptr<fake_writer_adapter> writer_adapter;
	std::shared_ptr<http::detail::body_reader_state> reader;
	std::shared_ptr<http::detail::body_writer_state> writer;
	http::response_head response;
	std::vector<http::response_head> informationals;
	bool cancelled = false;
};

class fake_session final : public http::detail::client_session
{
public:
	explicit fake_session(executor_type executor) : executor_(std::move(executor)) {}
	executor_type get_executor() const noexcept override { return executor_; }
	http::protocol selected_protocol() const noexcept override {
		return http::protocol::v1_1;
	}
	size_t available_stream_capacity() const noexcept override { return 1; }
	bool draining() const noexcept override { return false; }
	bool closed() const noexcept override { return false; }

	std::shared_ptr<http::detail::stream> open_sync(
		http::request_head head, http::body_spec spec,
		riwo::error_code &error) noexcept override
	{
		error.clear();
		last_head = std::move(head);
		++open_count;
		auto result = std::make_shared<fake_stream>(std::move(spec));
		last_stream = result;
		return result;
	}

	void open(http::request_head head, http::body_spec spec,
		open_handler handler) override
	{
		riwo::error_code error;
		auto result = open_sync(std::move(head), std::move(spec), error);
		std::move(handler)(error, std::move(result));
	}

	void graceful_shutdown(shutdown_handler handler) override {
		std::move(handler)(riwo::error_code{});
	}

	executor_type executor_;
	std::optional<http::request_head> last_head;
	std::shared_ptr<fake_stream> last_stream;
	size_t open_count = 0;
};

void built_in_sources()
{
	http::memory_body_source source(riwo::const_buffer(
		std::string_view("abcdef")));
	std::array<char,4> data{};
	riwo::error_code error;
	size_t size = 0;
	source.read_some(riwo::buffer(data), [&](riwo::error_code value, size_t count) {
		error = value;
		size = count;
	});
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(size, 4U);
	RIWO_TEST_CHECK_EQ(std::string_view(data.data(), size), "abcd");
	source.read_some(riwo::buffer(data), [&](riwo::error_code value, size_t count) {
		error = value;
		size = count;
	});
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(size, 2U);
	source.read_some(riwo::buffer(data), [&](riwo::error_code value, size_t count) {
		error = value;
		size = count;
	});
	RIWO_TEST_CHECK_EQ(error, asio::error::make_error_code(asio::error::eof));
	RIWO_TEST_CHECK_EQ(size, 0U);
	source.rewind([&](riwo::error_code value) { error = value; });
	RIWO_TEST_CHECK(!error);

	auto path = std::filesystem::temp_directory_path() /
		"riwo-http-exp-phase-a-source.bin";
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << "file-data";
	}
	{
		http::file_body_source file(path);
		RIWO_TEST_CHECK_EQ(file.size().value(), 9U);
		std::array<char,16> file_data{};
		file.read_some(riwo::buffer(file_data),
		[&](riwo::error_code value, size_t count) {
			error = value;
			size = count;
		});
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK_EQ(std::string_view(file_data.data(), size), "file-data");
	}
	std::filesystem::remove(path);
}

void reader_state_machine()
{
	riwo::io_context_t context;
	http::body_reader unbound(context);
	std::array<char,8> data{};
	riwo::error_code error;
	RIWO_TEST_CHECK_EQ(unbound.read_some(riwo::buffer(data), error), 0U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));

	http::fields trailers;
	trailers.add("Digest", "sha-256=value");
	auto adapter = std::make_shared<fake_reader_adapter>("payload", trailers);
	auto state = http::detail::make_body_reader_state(adapter, true);
	http::body_reader reader(context, state);
	(void)reader.trailers(error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));

	auto size = reader.read_some(riwo::buffer(data), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(std::string_view(data.data(), size), "payload");
	RIWO_TEST_CHECK_EQ(reader.read_some(riwo::buffer(data), error), 0U);
	RIWO_TEST_CHECK_EQ(error, asio::error::make_error_code(asio::error::eof));
	auto received_trailers = reader.trailers(error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(received_trailers.find_first("digest").value(),
		"sha-256=value");

	auto overlap_adapter = std::make_shared<fake_reader_adapter>("xy");
	overlap_adapter->hold();
	auto overlap = http::detail::make_body_reader_state(overlap_adapter, true);
	bool first = false;
	bool second = false;
	overlap->read_some(riwo::buffer(data),
		http::detail::body_reader_state::read_handler(
		[&](riwo::error_code value, size_t count) {
			RIWO_TEST_CHECK(!value);
			RIWO_TEST_CHECK_EQ(count, 2U);
			first = true;
		}));
	overlap->read_some(riwo::buffer(data),
		http::detail::body_reader_state::read_handler(
		[&](riwo::error_code value, size_t) {
			RIWO_TEST_CHECK_EQ(value,
				http::make_error_code(http::errc::operation_in_progress));
			second = true;
		}));
	RIWO_TEST_CHECK(!first);
	RIWO_TEST_CHECK(second);
	overlap_adapter->complete();
	RIWO_TEST_CHECK(first);
}

void writer_state_machine()
{
	riwo::io_context_t context;
	riwo::error_code error;
	auto short_adapter = std::make_shared<fake_writer_adapter>();
	auto short_state = http::detail::make_body_writer_state(short_adapter,
		http::body_spec::known(3), true);
	http::body_writer short_writer(context, short_state);
	RIWO_TEST_CHECK_EQ(short_writer.write(
		riwo::buffer(std::string_view("ab")), error), 2U);
	RIWO_TEST_CHECK(!error);
	short_writer.finish({}, error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::body_length_mismatch));
	RIWO_TEST_CHECK_EQ(short_writer.write(
		riwo::buffer(std::string_view("c")), error), 0U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));

	auto excess_adapter = std::make_shared<fake_writer_adapter>();
	auto excess_state = http::detail::make_body_writer_state(excess_adapter,
		http::body_spec::known(3), true);
	http::body_writer excess(context, excess_state);
	RIWO_TEST_CHECK_EQ(excess.write(
		riwo::buffer(std::string_view("abcd")), error), 3U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::body_length_mismatch));
	RIWO_TEST_CHECK_EQ(excess_adapter->bytes_, "abc");

	auto trailer_adapter = std::make_shared<fake_writer_adapter>();
	auto trailer_state = http::detail::make_body_writer_state(trailer_adapter,
		http::body_spec::streaming({{"Digest"}}), true);
	http::body_writer trailer_writer(context, trailer_state);
	RIWO_TEST_CHECK_EQ(trailer_writer.write(
		riwo::buffer(std::string_view("x")), error), 1U);
	http::fields values;
	values.add("digest", "value");
	trailer_writer.finish(values, error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK(trailer_adapter->finished_);

	auto borrowed_adapter = std::make_shared<fake_writer_adapter>();
	borrowed_adapter->hold();
	auto borrowed_state = http::detail::make_body_writer_state(borrowed_adapter,
		http::body_spec::known(3), true);
	http::body_writer borrowed_writer(context, borrowed_state);
	std::string borrowed_body = "abc";
	bool borrowed_done = false;
	borrowed_writer.write(riwo::buffer(borrowed_body),
	[&](riwo::error_code value, size_t count) {
		RIWO_TEST_CHECK(!value);
		RIWO_TEST_CHECK_EQ(count, 3U);
		borrowed_done = true;
	});
	borrowed_body = "xyz";
	borrowed_adapter->complete();
	RIWO_TEST_CHECK(borrowed_done);
	RIWO_TEST_CHECK_EQ(borrowed_adapter->bytes_, "xyz");
	borrowed_writer.finish({}, error);
	RIWO_TEST_CHECK(!error);

	auto detached_adapter = std::make_shared<fake_writer_adapter>();
	detached_adapter->hold();
	auto detached_state = http::detail::make_body_writer_state(detached_adapter,
		http::body_spec::known(3), true);
	http::body_writer detached_writer(context, detached_state);
	std::string detached_body = "abc";
	detached_writer.write(riwo::buffer(detached_body), riwo::detached);
	detached_body = "xyz";
	detached_adapter->complete();
	RIWO_TEST_CHECK_EQ(detached_adapter->bytes_, "abc");
	detached_writer.finish({}, error);
	RIWO_TEST_CHECK(!error);

	auto overlap_adapter = std::make_shared<fake_writer_adapter>();
	overlap_adapter->hold();
	auto overlap = http::detail::make_body_writer_state(overlap_adapter,
		http::body_spec::streaming(), true);
	bool first = false;
	bool second = false;
	overlap->write(riwo::buffer(std::string_view("x")),
		http::detail::body_writer_state::write_handler(
		[&](riwo::error_code value, size_t count) {
			RIWO_TEST_CHECK(!value);
			RIWO_TEST_CHECK_EQ(count, 1U);
			first = true;
		}));
	overlap->write(riwo::buffer(std::string_view("y")),
		http::detail::body_writer_state::write_handler(
		[&](riwo::error_code value, size_t) {
			RIWO_TEST_CHECK_EQ(value,
				http::make_error_code(http::errc::operation_in_progress));
			second = true;
		}));
	RIWO_TEST_CHECK(!first);
	RIWO_TEST_CHECK(second);
	overlap_adapter->complete();
	RIWO_TEST_CHECK(first);
}

void client_open_and_metadata()
{
	riwo::io_context_t context;
	auto session = std::make_shared<fake_session>(context.get_executor());
	http::basic_client<riwo::io_context_t::executor_type> client(context, session);
	http::request request(http::method::post,
		"HTTP://Example.Test:80/items/a%20b?q=one%20two");
	request.headers().add("Content-Type", "application/octet-stream");
	riwo::error_code error;
	auto exchange = client.open(std::move(request),
		http::body_spec::known(3), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(session->open_count, 1U);
	RIWO_TEST_CHECK_EQ(session->last_head->scheme, "http");
	RIWO_TEST_CHECK_EQ(session->last_head->authority, "example.test");
	RIWO_TEST_CHECK_EQ(session->last_head->target,
		"/items/a%20b?q=one%20two");
	RIWO_TEST_CHECK_EQ(session->last_head->headers.find_first("content-type").value(),
		"application/octet-stream");

	std::array<char,8> data{};
	RIWO_TEST_CHECK_EQ(exchange.response_body().read_some(
		riwo::buffer(data), error), 0U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));
	auto head = exchange.wait_response(error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(head.status, http::status::created);
	auto size = exchange.response_body().read_some(riwo::buffer(data), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(std::string_view(data.data(), size), "reply");

	http::request with_body(http::method::post, "http://example.test/");
	with_body.body("bound");
	(void)client.open(std::move(with_body), http::body_spec::streaming(), error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));
	RIWO_TEST_CHECK_EQ(session->open_count, 1U);

	http::request fragmented(http::method::get,
		"http://example.test/path#fragment");
	(void)client.open(std::move(fragmented), http::body_spec::empty(), error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::invalid_target));

	http::request send_request(http::method::post, "http://example.test/send");
	send_request.body("abc");
	auto reply = client.send(std::move(send_request), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(reply.status(), http::status::created);
	RIWO_TEST_CHECK_EQ(session->last_stream->writer_adapter->bytes_, "abc");

	std::string borrowed_request_body = "before";
	http::request borrowed_request(http::method::post,
		"http://example.test/send-borrowed");
	borrowed_request.body(riwo::buffer(borrowed_request_body));
	borrowed_request_body = "after!";
	(void)client.send(std::move(borrowed_request), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(session->last_stream->writer_adapter->bytes_, "after!");

	const std::array binary_body {'a', '\0', 'b'};
	http::request binary_request(http::method::post,
		"http://example.test/send-binary");
	binary_request.body(riwo::buffer(binary_body));
	(void)client.send(std::move(binary_request), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(session->last_stream->writer_adapter->bytes_,
		std::string("a\0b", 3));

	auto fetched = client.fetch(
		http::request(http::method::get, "http://example.test/fetch"), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(std::string_view(
		reinterpret_cast<const char*>(fetched.buffered_body().data()),
		fetched.buffered_body().size()), "reply");

	bool asynchronous = false;
	http::request async_request(http::method::post,
		"http://example.test/async-send");
	async_request.body("async");
	client.send(std::move(async_request),
	[&](riwo::error_code send_error,
		http::basic_reply<riwo::io_context_t::executor_type> async_reply)
	{
		RIWO_TEST_CHECK(!send_error);
		RIWO_TEST_CHECK_EQ(async_reply.status(), http::status::created);
		asynchronous = true;
	});
	context.run();
	RIWO_TEST_CHECK(asynchronous);
	RIWO_TEST_CHECK_EQ(session->last_stream->writer_adapter->bytes_, "async");

	context.restart();
	bool asynchronous_fetch = false;
	client.fetch(http::request(http::method::get,
		"http://example.test/async-fetch"),
	[&](riwo::error_code fetch_error,
		http::basic_reply<riwo::io_context_t::executor_type> async_reply)
	{
		RIWO_TEST_CHECK(!fetch_error);
		RIWO_TEST_CHECK_EQ(std::string_view(
			reinterpret_cast<const char*>(async_reply.buffered_body().data()),
			async_reply.buffered_body().size()), "reply");
		asynchronous_fetch = true;
	});
	context.run();
	RIWO_TEST_CHECK(asynchronous_fetch);
}

void reply_convenience_operations()
{
	riwo::io_context_t context;
	riwo::error_code error;
	auto read_adapter = std::make_shared<fake_reader_adapter>("read-all");
	http::basic_reply<riwo::io_context_t::executor_type> reply(
		context, http::protocol::v1_1, {http::status::ok, {}},
		http::detail::make_body_reader_state(read_adapter, true));
	auto bytes = reply.read_all(error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(std::string_view(
		reinterpret_cast<const char*>(bytes.data()), bytes.size()), "read-all");

	auto save_adapter = std::make_shared<fake_reader_adapter>("save-file");
	http::basic_reply<riwo::io_context_t::executor_type> file_reply(
		context, http::protocol::v1_1, {http::status::ok, {}},
		http::detail::make_body_reader_state(save_adapter, true));
	auto path = std::filesystem::temp_directory_path() /
		"riwo-http-exp-phase-a-reply.bin";
	RIWO_TEST_CHECK_EQ(file_reply.save_file(path, error), 9U);
	RIWO_TEST_CHECK(!error);
	std::ifstream input(path, std::ios::binary);
	std::string contents((std::istreambuf_iterator<char>(input)), {});
	input.close();
	RIWO_TEST_CHECK_EQ(contents, "save-file");
	std::filesystem::remove(path);
}

void server_exchange_state_machine()
{
	riwo::io_context_t context;
	auto stream = std::make_shared<fake_stream>(http::body_spec::empty());
	stream->writer = http::detail::make_body_writer_state(
		stream->writer_adapter, http::body_spec::empty(), false);
	http::basic_server_exchange<riwo::io_context_t::executor_type> exchange(
		context, stream, http::request_head {
			.method = http::method::post,
			.target = "/upload"
		});
	RIWO_TEST_CHECK_EQ(exchange.request().method.known().value(),
		http::method::post);
	RIWO_TEST_CHECK_EQ(exchange.request().target, "/upload");
	riwo::error_code error;
	RIWO_TEST_CHECK_EQ(exchange.response_body().write(
			riwo::buffer(std::string_view("x")), error), 0U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));
	exchange.respond({http::status::ok, {}}, http::body_spec::known(3), error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(exchange.response_body().write(
			riwo::buffer(std::string_view("yes")), error), 3U);
	RIWO_TEST_CHECK(!error);
	exchange.response_body().finish({}, error);
	RIWO_TEST_CHECK(!error);
	exchange.respond({http::status::ok, {}}, http::body_spec::empty(), error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_permitted));

	auto buffered_stream = std::make_shared<fake_stream>(
		http::body_spec::empty());
	buffered_stream->writer = http::detail::make_body_writer_state(
		buffered_stream->writer_adapter, http::body_spec::empty(), false);
	http::basic_server_exchange<riwo::io_context_t::executor_type> buffered(
		context, buffered_stream, {});
	buffered.respond({http::status::ok, {}}, "hello", error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(buffered_stream->writer_adapter->bytes_, "hello");
	RIWO_TEST_CHECK(buffered_stream->writer_adapter->finished_);

	auto borrowed_stream = std::make_shared<fake_stream>(
		http::body_spec::empty());
	borrowed_stream->writer_adapter->hold();
	borrowed_stream->writer = http::detail::make_body_writer_state(
		borrowed_stream->writer_adapter, http::body_spec::empty(), false);
	http::basic_server_exchange<riwo::io_context_t::executor_type>
		borrowed_exchange(context, borrowed_stream, {});
	std::string borrowed_response_body = "abc";
	bool response_done = false;
	borrowed_exchange.respond({http::status::ok, {}},
		riwo::buffer(borrowed_response_body),
	[&](riwo::error_code value) {
		RIWO_TEST_CHECK(!value);
		response_done = true;
	});
	borrowed_response_body = "xyz";
	borrowed_stream->writer_adapter->complete();
	RIWO_TEST_CHECK(response_done);
	RIWO_TEST_CHECK_EQ(borrowed_stream->writer_adapter->bytes_, "xyz");

	auto detached_stream = std::make_shared<fake_stream>(
		http::body_spec::empty());
	detached_stream->writer_adapter->hold();
	detached_stream->writer = http::detail::make_body_writer_state(
		detached_stream->writer_adapter, http::body_spec::empty(), false);
	http::basic_server_exchange<riwo::io_context_t::executor_type>
		detached_exchange(context, detached_stream, {});
	std::string detached_response_body = "abc";
	detached_exchange.respond({http::status::ok, {}},
		riwo::buffer(detached_response_body), riwo::detached);
	detached_response_body = "xyz";
	detached_stream->writer_adapter->complete();
	RIWO_TEST_CHECK_EQ(detached_stream->writer_adapter->bytes_, "abc");

	auto informational_stream = std::make_shared<fake_stream>(
		http::body_spec::empty());
	http::basic_server_exchange<riwo::io_context_t::executor_type> informational(
		context, informational_stream, {});
	informational.inform({http::status::continue_upload, {}}, error);
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(informational_stream->informationals.size(), 1U);
	informational.inform({http::status::ok, {}}, error);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::invalid_status));
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"built-in body sources", built_in_sources},
		{"body reader state machine", reader_state_machine},
		{"body writer state machine", writer_state_machine},
		{"client open metadata", client_open_and_metadata},
		{"reply convenience operations", reply_convenience_operations},
		{"server exchange state machine", server_exchange_state_machine}
	});
}
