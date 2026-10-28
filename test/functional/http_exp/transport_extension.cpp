// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>
#include <deque>

namespace
{

namespace http = riwo::http_exp;
using executor_type = riwo::io_context_t::executor_type;

struct memory_state
{
	std::string input;
	std::string output;
	size_t offset = 0;
	bool open = true;
	bool deferred = false;
	riwo::error_code read_error {};
	riwo::error_code write_error {};
};

struct memory_stream
{
	using executor_type = riwo::io_context_t::executor_type;
	executor_type executor;
	std::shared_ptr<memory_state> state;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor; }
	void read_some(riwo::mutable_buffer buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		auto operation = [state = state, buffer,
			handler = std::move(handler)]() mutable
		{
			if( state->read_error )
			{
				auto error = state->read_error;
				state->read_error.clear();
				return std::move(handler)(error, 0);
			}
			if( state->offset == state->input.size() )
				return std::move(handler)(
					asio::error::make_error_code(asio::error::eof), 0);
			const auto count = std::min(buffer.size(),
				state->input.size() - state->offset);
			std::memcpy(buffer.data(), state->input.data() + state->offset, count);
			state->offset += count;
			std::move(handler)(riwo::error_code{}, count);
		};
		if( state->deferred ) asio::post(executor, std::move(operation));
		else operation();
	}
	void write(riwo::const_buffer buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		auto operation = [state = state, buffer,
			handler = std::move(handler)]() mutable
		{
			if( state->write_error )
			{
				auto error = state->write_error;
				state->write_error.clear();
				return std::move(handler)(error, 0);
			}
			state->output.append(static_cast<const char *>(buffer.data()),
				buffer.size());
			std::move(handler)(riwo::error_code{}, buffer.size());
		};
		if( state->deferred ) asio::post(executor, std::move(operation));
		else operation();
	}
	void close(asio::any_completion_handler<void(riwo::error_code)> handler)
	{
		auto operation = [state = state,
			handler = std::move(handler)]() mutable {
			state->open = false;
			std::move(handler)(riwo::error_code{});
		};
		if( state->deferred ) asio::post(executor, std::move(operation));
		else operation();
	}
	void cancel() noexcept { state->open = false; }
	[[nodiscard]] bool is_open() const noexcept { return state->open; }
	[[nodiscard]] http::transport_metadata metadata() const {
		return {.remote_endpoint = "127.0.0.1:8080"};
	}
};

static_assert(http::concepts::byte_stream_model<memory_stream>);
static_assert(!std::copy_constructible<http::basic_byte_stream<executor_type>>);
static_assert(std::move_constructible<http::basic_byte_stream<executor_type>>);

class fake_connector final : public http::basic_transport_connector<executor_type>
{
public:
	fake_connector(executor_type executor, std::shared_ptr<memory_state> state) :
		executor_(executor), state_(std::move(state)) {}
	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] riwo::sys_expected<result_t> connect_sync(
		const http::connect_request &request) noexcept override
	{
		{
			std::lock_guard lock(mutex_);
			last_request_ = request;
		}
		return result_t {
			http::make_byte_stream(memory_stream{executor_, state_}),
			http::protocol::v1_1, {}, {}
		};
	}
	void connect(http::connect_request request, connect_handler_t handler) override
	{
		{
			std::lock_guard lock(mutex_);
			last_request_ = request;
		}
		asio::post(executor_, [executor = executor_, state = state_,
			handler = std::move(handler)]() mutable {
			std::move(handler)(riwo::error_code{}, result_t {
				http::make_byte_stream(memory_stream{executor, std::move(state)}),
				http::protocol::v1_1, {}, {}
			});
		});
	}
	void cancel() noexcept override {}
	[[nodiscard]] std::optional<http::connect_request> last_request() const {
		std::lock_guard lock(mutex_);
		return last_request_;
	}

private:
	executor_type executor_;
	std::shared_ptr<memory_state> state_;
	mutable std::mutex mutex_;
	std::optional<http::connect_request> last_request_;
};

class fake_acceptor
{
public:
	using executor_type = riwo::io_context_t::executor_type;
	using acceptor_wrap_t = http::basic_acceptor_wrap<executor_type>;
	using acceptor_t = typename acceptor_wrap_t::acceptor_t;
	using accepted_t = typename acceptor_wrap_t::accepted_t;
	using transport_t = typename acceptor_wrap_t::transport_t;
	using accept_handler_t = typename acceptor_wrap_t::accept_handler_t;

	explicit fake_acceptor(executor_type executor,
		std::shared_ptr<memory_state> state = {},
		std::vector<std::byte> initial_input = {}) :
		acceptor_(executor), state_(std::move(state)),
		initial_input_(std::move(initial_input))
	{
		acceptor_.open(asio::ip::tcp::v4());
		acceptor_.bind({asio::ip::tcp::v4(), 0});
	}
	[[nodiscard]] executor_type get_executor() noexcept {
		return acceptor_.get_executor();
	}
	void accept(accept_handler_t handler)
	{
		if( not state_ or delivered_ )
		{
			pending_.emplace(std::move(handler));
			return;
		}
		delivered_ = true;
		auto executor = get_executor();
		asio::post(executor, [executor, state = state_,
			initial = std::move(initial_input_), handler = std::move(handler)]() mutable {
			transport_t transport {
				http::make_byte_stream(memory_stream{executor, std::move(state)}),
				http::protocol::v1_1,
				{.local_endpoint = "192.0.2.2:8080",
				 .remote_endpoint = "192.0.2.1:4242"},
				std::move(initial)};
			std::move(handler)(riwo::error_code{}, accepted_t(
				std::in_place_type<transport_t>, std::move(transport)));
		});
	}
	void cancel() noexcept {
		pending_.reset();
		riwo::error_code ignored;
		acceptor_.cancel(ignored);
		acceptor_.close(ignored);
	}
	[[nodiscard]] const acceptor_t &acceptor() const noexcept {
		return acceptor_;
	}
	[[nodiscard]] acceptor_t &acceptor() noexcept { return acceptor_; }

private:
	acceptor_t acceptor_;
	std::shared_ptr<memory_state> state_;
	std::vector<std::byte> initial_input_;
	bool delivered_ = false;
	std::optional<accept_handler_t> pending_;
};

[[nodiscard]] std::vector<std::byte> bytes(std::string_view value)
{
	std::vector<std::byte> result(value.size());
	std::memcpy(result.data(), value.data(), value.size());
	return result;
}

static_assert(std::constructible_from<http::basic_client<executor_type>,
	executor_type,std::shared_ptr<http::basic_transport_connector<executor_type>>>);
static_assert(std::constructible_from<http::basic_acceptor_wrap<executor_type>,
	fake_acceptor>);
static_assert(std::constructible_from<http::basic_server<executor_type>,
	http::basic_acceptor_wrap<executor_type>&&>);

void byte_stream_erasure()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	state->input = "input";
	auto stream = http::make_byte_stream(memory_stream{
		context.get_executor(), state});
	RIWO_TEST_CHECK(stream.is_open());
	RIWO_TEST_CHECK(!stream.capabilities().synchronous);
	std::array<char,8> input {};
	size_t read = 0;
	stream.read_some(riwo::buffer(input),
	[&](riwo::error_code error, size_t size) {
		RIWO_TEST_CHECK(!error);
		read = size;
	});
	RIWO_TEST_CHECK_EQ(read, 5U);
	RIWO_TEST_CHECK_EQ(std::string_view(input.data(), read), "input");
	size_t written = 0;
	stream.write(riwo::buffer(std::string_view("output")),
	[&](riwo::error_code error, size_t size) {
		RIWO_TEST_CHECK(!error);
		written = size;
	});
	RIWO_TEST_CHECK_EQ(written, 6U);
	RIWO_TEST_CHECK_EQ(state->output, "output");
	riwo::error_code error;
	RIWO_TEST_CHECK_EQ(stream.read_some_sync(riwo::buffer(input), error), 0U);
	RIWO_TEST_CHECK_EQ(error,
		http::make_error_code(http::errc::operation_not_supported));

	auto moved_stream = std::move(stream);
	RIWO_TEST_CHECK(stream.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(!stream.is_open());
	RIWO_TEST_CHECK(moved_stream.is_open());
	bool empty_read_completed = false;
	stream.read_some(riwo::buffer(input),
	[&](riwo::error_code empty_error, size_t size) {
		RIWO_TEST_CHECK_EQ(empty_error,
			http::make_error_code(http::errc::operation_not_permitted));
		RIWO_TEST_CHECK_EQ(size, 0U);
		empty_read_completed = true;
	});
	RIWO_TEST_CHECK(empty_read_completed);
	auto assigned_stream = http::make_byte_stream(memory_stream{
		context.get_executor(), std::make_shared<memory_state>()});
	assigned_stream = std::move(moved_stream);
	RIWO_TEST_CHECK(moved_stream.get_executor() == context.get_executor());
	RIWO_TEST_CHECK(!moved_stream.is_open());
	RIWO_TEST_CHECK(assigned_stream.is_open());

	http::transport_metadata metadata;
	metadata.attributes.emplace("http.absolute_form", true);
	RIWO_TEST_CHECK_EQ(metadata.attributes.at(
		"http.absolute_form").to_bool().value(), true);
}

void injected_connector_and_acceptor_compile()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	auto connector = std::make_shared<fake_connector>(context.get_executor(), state);
	http::basic_client<executor_type> client(context.get_executor(), connector);
	RIWO_TEST_CHECK(client.get_executor() == context.get_executor());
	auto acceptor = http::basic_acceptor_wrap<executor_type>(
		fake_acceptor(context.get_executor()));
	http::basic_server<executor_type> server(std::move(acceptor));
	riwo::error_code error;
	server.start(error);
	RIWO_TEST_CHECK(!error);
	server.stop();
}

void byte_stream_failure_injection()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	state->read_error = asio::error::make_error_code(
		asio::error::connection_reset);
	state->write_error = asio::error::make_error_code(asio::error::broken_pipe);
	auto stream = http::make_byte_stream(memory_stream{
		context.get_executor(), state});
	std::array<char,4> buffer {};
	bool read_completed = false;
	stream.read_some(riwo::buffer(buffer),
	[&](riwo::error_code error, size_t size) {
		RIWO_TEST_CHECK_EQ(error, asio::error::make_error_code(
			asio::error::connection_reset));
		RIWO_TEST_CHECK_EQ(size, 0U);
		read_completed = true;
	});
	bool write_completed = false;
	stream.write(riwo::buffer(std::string_view("data")),
	[&](riwo::error_code error, size_t size) {
		RIWO_TEST_CHECK_EQ(error, asio::error::make_error_code(
			asio::error::broken_pipe));
		RIWO_TEST_CHECK_EQ(size, 0U);
		write_completed = true;
	});
	RIWO_TEST_CHECK(read_completed);
	RIWO_TEST_CHECK(write_completed);
	stream.cancel();
	RIWO_TEST_CHECK(!stream.is_open());
}

void injected_connector_drives_client()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	state->deferred = true;
	state->input = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
	auto connector = std::make_shared<fake_connector>(context.get_executor(), state);
	http::basic_client<executor_type> client(context.get_executor(), connector);
	bool completed = false;
	client.fetch(http::request(http::method::get, "http://example.test/custom"),
	[&](riwo::error_code error, http::basic_reply<executor_type> reply) mutable {
		RIWO_TEST_CHECK(!error);
		const auto body = reply.buffered_body();
		RIWO_TEST_CHECK_EQ(std::string_view(
			reinterpret_cast<const char *>(body.data()), body.size()), "ok");
		completed = true;
		context.stop();
	});
	context.run();
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK(state->output.starts_with("GET /custom HTTP/1.1\r\n"));
}

void request_proxy_override_reaches_connector()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	state->deferred = true;
	state->input = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
	auto connector = std::make_shared<fake_connector>(context.get_executor(), state);
	http::client_config config;
	config.proxy.forward_proxy = riwo::url("http://base-proxy.test:8080");
	http::basic_client<executor_type> client(context.get_executor(), connector,
		config);
	http::proxy_policy override;
	override.forward_proxy = riwo::url("socks5://127.0.0.1:1080");
	override.type = http::proxy_type::socks5;
	override.username = "alice";
	override.password = "secret";
	http::request request(http::method::get, "http://example.test/resource");
	request.set_proxy(std::move(override));
	bool completed = false;
	client.fetch(std::move(request),
	[&](riwo::error_code error, http::basic_reply<executor_type> reply) mutable {
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK_EQ(reply.status(), http::status::no_content);
		completed = true;
		context.stop();
	});
	context.run();
	RIWO_TEST_CHECK(completed);
	auto observed = connector->last_request();
	RIWO_TEST_CHECK(observed.has_value());
	RIWO_TEST_CHECK(observed->proxy.forward_proxy.has_value());
	RIWO_TEST_CHECK_EQ(observed->proxy.forward_proxy->protocol(), "socks5");
	RIWO_TEST_CHECK_EQ(observed->proxy.type, http::proxy_type::socks5);
	RIWO_TEST_CHECK_EQ(observed->proxy.username.value(), "alice");
	RIWO_TEST_CHECK_EQ(observed->proxy.password.value(), "secret");

	riwo::io_context_t bypass_context;
	auto bypass_state = std::make_shared<memory_state>();
	bypass_state->deferred = true;
	bypass_state->input =
		"HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
	auto bypass_connector = std::make_shared<fake_connector>(
		bypass_context.get_executor(), bypass_state);
	http::basic_client<executor_type> bypass_client(
		bypass_context.get_executor(), bypass_connector);
	http::proxy_policy bypass_policy;
	bypass_policy.forward_proxy = riwo::url("http://proxy.test:8080");
	bypass_policy.no_proxy = {".example.test"};
	http::request bypass_request(http::method::get,
		"http://api.example.test/resource");
	bypass_request.set_proxy(std::move(bypass_policy));
	bypass_client.fetch(std::move(bypass_request),
	[&](riwo::error_code error, http::basic_reply<executor_type>) mutable {
		RIWO_TEST_CHECK(!error);
		bypass_context.stop();
	});
	bypass_context.run();
	auto bypass_observed = bypass_connector->last_request();
	RIWO_TEST_CHECK(bypass_observed.has_value());
	RIWO_TEST_CHECK(!bypass_observed->proxy.forward_proxy.has_value());
}

void injected_acceptor_replays_initial_input()
{
	riwo::io_context_t context;
	auto state = std::make_shared<memory_state>();
	state->deferred = true;
	auto acceptor = http::basic_acceptor_wrap<executor_type>(fake_acceptor(
		context.get_executor(), state,
		bytes("GET /custom HTTP/1.1\r\nHost: example.test\r\n\r\n")));
	http::basic_server<executor_type> server(std::move(acceptor));
	server.on_request<http::method::get>("/custom",
	[&context](http::basic_server_exchange<executor_type> &exchange)
		-> riwo::awaitable<void>
	{
		RIWO_TEST_CHECK_EQ(exchange.remote_address(), "192.0.2.1");
		RIWO_TEST_CHECK_EQ(exchange.remote_port(), 4242U);
		auto local_endpoint = exchange.local_endpoint();
		RIWO_TEST_CHECK(local_endpoint.has_value());
		RIWO_TEST_CHECK_EQ(*local_endpoint,
			asio::ip::tcp::endpoint(asio::ip::make_address("192.0.2.2"), 8080));
		co_await exchange.respond({http::status::ok, {}},
			std::string_view("accepted"), riwo::use_awaitable);
		context.stop();
	});
	riwo::error_code error;
	server.start(error);
	RIWO_TEST_CHECK(!error);
	context.run();
	server.stop();
	RIWO_TEST_CHECK(state->output.starts_with("HTTP/1.1 200 OK\r\n"));
	RIWO_TEST_CHECK(state->output.ends_with("accepted"));
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"byte stream type erasure", byte_stream_erasure},
		{"byte stream failure injection", byte_stream_failure_injection},
		{"connector and acceptor injection", injected_connector_and_acceptor_compile},
		{"custom connector drives client", injected_connector_drives_client},
		{"request proxy override reaches connector",
			request_proxy_override_reaches_connector},
		{"custom acceptor replays initial input",
			injected_acceptor_replays_initial_input}
	});
}
