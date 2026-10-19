// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/client/detail/connector.h>
#include <riwo/http_exp/client/detail/session_pool.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/preface.h>

namespace
{

namespace http = riwo::http_exp;
namespace detail = riwo::http_exp::detail;

class fake_stream final : public detail::stream
{
public:
	explicit fake_stream(http::protocol version) : m_version(version) {}
	[[nodiscard]] http::protocol selected_protocol() const noexcept override {
		return m_version;
	}
	void cancel() noexcept override { cancelled = true; }
	http::protocol m_version;
	bool cancelled = false;
};

class fake_session final : public detail::client_session
{
public:
	using open_handler = detail::client_session::open_handler;

	fake_session(executor_type executor, http::protocol version, size_t id) :
		m_id(id), m_executor(std::move(executor)), m_version(version)
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_executor;
	}
	[[nodiscard]] http::protocol selected_protocol() const noexcept override {
		return m_version;
	}
	[[nodiscard]] size_t available_stream_capacity() const noexcept override {
		return busy || draining_ || closed_ ? 0 : 1;
	}
	[[nodiscard]] bool draining() const noexcept override { return draining_; }
	[[nodiscard]] bool closed() const noexcept override { return closed_; }

	[[nodiscard]] bool try_reserve_stream() noexcept override
	{
		if( busy || draining_ || closed_ ) return false;
		busy = true;
		return true;
	}

	void release_stream(bool reusable) noexcept override
	{
		if( !busy ) return;
		busy = false;
		if( !reusable ) closed_ = true;
		++release_count;
		if( release_handler ) release_handler(reusable);
	}

	void set_release_handler(std::function<void(bool)> handler) override {
		release_handler = std::move(handler);
	}

	std::shared_ptr<detail::stream> open_sync(
		http::request_head head, http::body_spec,
		riwo::error_code &error) noexcept override
	{
		if( busy )
		{
			error = http::make_error_code(http::errc::operation_in_progress);
			return {};
		}
		busy = true;
		opened_targets.push_back(std::move(head.target));
		auto result = std::make_shared<fake_stream>(m_version);
		last_stream = result;
		return result;
	}

	void open(http::request_head head, http::body_spec spec,
		open_handler handler) override
	{
		riwo::error_code error;
		auto result = open_sync(std::move(head), std::move(spec), error);
		asio::post(m_executor,
		[handler = std::move(handler), error, result = std::move(result)]()
		mutable {
			std::move(handler)(error, std::move(result));
		});
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		draining_ = true;
		closed_ = true;
		std::move(handler)(riwo::error_code{});
	}

	size_t id() const noexcept { return m_id; }

	size_t m_id;
	bool busy = false;
	bool draining_ = false;
	bool closed_ = false;
	size_t release_count = 0;
	std::vector<std::string> opened_targets;
	std::shared_ptr<detail::stream> last_stream;
	std::function<void(bool)> release_handler;

private:
	executor_type m_executor;
	http::protocol m_version;
};

class fake_connector final : public detail::connector
{
public:
	explicit fake_connector(executor_type executor) :
		detail::connector(std::move(executor))
	{
	}

	[[nodiscard]] riwo::sys_expected<detail::negotiated_transport> connect(
		const detail::session_key &key,
		const http::client_config &config) noexcept override
	{
		riwo::ignore_unused(config);
		last_key = key;
		++connect_count;
		return detail::negotiated_transport {
			.selected_protocol = selected
		};
	}

	detail::session_key last_key;
	http::protocol selected = http::protocol::v1_1;
	size_t connect_count = 0;
};

class fake_factory final : public detail::session_factory
{
public:
	explicit fake_factory(asio::any_io_executor executor) :
		m_executor(std::move(executor))
	{
	}

	riwo::sys_expected<std::shared_ptr<detail::client_session>> create(
		const detail::session_key &key, detail::negotiated_transport transport,
		const http::client_config &config) override
	{
		riwo::ignore_unused(config);
		keys.push_back(key);
		protocols.push_back(transport.selected_protocol);
		auto session = std::make_shared<fake_session>(m_executor,
			transport.selected_protocol, sessions.size());
		sessions.push_back(session);
		return std::shared_ptr<detail::client_session>(session);
	}

	asio::any_io_executor m_executor;
	std::vector<detail::session_key> keys;
	std::vector<http::protocol> protocols;
	std::vector<std::shared_ptr<fake_session>> sessions;
};

void drain(riwo::io_context_t &context)
{
	context.restart();
	context.run();
}

void drain_for(riwo::io_context_t &context,
	std::chrono::milliseconds duration)
{
	context.restart();
	riwo::ignore_unused(context.run_for(duration));
}

http::request_head make_head(std::string url)
{
	riwo::url value(url);
	return {
		.method = http::method::get,
		.url = value,
		.scheme = std::string(value.protocol()),
		.authority = std::string(value.host()),
		.target = std::string(value.encoded_path())
	};
}

void session_key_identity()
{
	http::client_config config;
	auto http = detail::make_session_key(
		make_head("HTTP://Example.Test/items"), config);
	RIWO_TEST_CHECK_EQ(http.scheme, "http");
	RIWO_TEST_CHECK_EQ(http.host, "example.test");
	RIWO_TEST_CHECK_EQ(http.port, 80U);
	RIWO_TEST_CHECK(!http.tls.enabled);

	auto https = detail::make_session_key(
		make_head("https://EXAMPLE.test:8443/"), config);
	RIWO_TEST_CHECK_EQ(https.scheme, "https");
	RIWO_TEST_CHECK_EQ(https.host, "example.test");
	RIWO_TEST_CHECK_EQ(https.port, 8443U);
	RIWO_TEST_CHECK(https.tls.enabled);

	auto tls_config = config;
	tls_config.tls.verify_peer = false;
	auto no_verify = detail::make_session_key(
		make_head("https://example.test/"), tls_config);
	RIWO_TEST_CHECK(no_verify != https);

	auto policy_config = config;
	policy_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	auto required = detail::make_session_key(
		make_head("http://example.test/"), policy_config);
	RIWO_TEST_CHECK(required != http);

	detail::session_key_hash hash;
	RIWO_TEST_CHECK_EQ(hash(http), hash(detail::make_session_key(
		make_head("http://EXAMPLE.TEST/other"), config)));
}

void protocol_negotiation()
{
	http::client_config config;
	auto key = detail::make_session_key(
		make_head("https://example.test/"), config);
	auto selected = detail::negotiate_protocol(config.protocols, key, false);
	RIWO_TEST_CHECK_EQ(selected.error(),
		http::make_error_code(http::errc::operation_not_supported));

	auto invalid = config.protocols;
	invalid.preference.clear();
	auto failed = detail::negotiate_protocol(invalid, key, true);
	RIWO_TEST_CHECK_EQ(failed.error(),
		http::make_error_code(http::errc::negotiation_failed));

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	auto h2_key = key;
	h2_key.protocols.preference = {
		http::protocol::v2, http::protocol::v1_1
	};
	auto h2 = detail::negotiate_protocol(http::protocol_policy {
		.preference = h2_key.protocols.preference,
		.requirement = http::protocol_requirement::automatic
	}, h2_key, true);
	RIWO_TEST_CHECK_EQ(h2.value(), http::protocol::v2);

	auto plain = detail::make_session_key(
		make_head("http://example.test/"), config);
	plain.protocols.preference = {
		http::protocol::v2, http::protocol::v1_1
	};
	auto automatic = detail::negotiate_protocol(http::protocol_policy {
		.preference = plain.protocols.preference,
		.requirement = http::protocol_requirement::automatic
	}, plain, true);
	RIWO_TEST_CHECK_EQ(automatic.value(), http::protocol::v1_1);

	auto prior = detail::negotiate_protocol(http::protocol_policy {
		.preference = plain.protocols.preference,
		.requirement = http::protocol_requirement::require_http2
	}, plain, true);
	RIWO_TEST_CHECK_EQ(prior.value(), http::protocol::v2);
#else
	auto plain = detail::make_session_key(
		make_head("http://example.test/"), config);
	auto value = detail::negotiate_protocol(config.protocols, plain, false);
	RIWO_TEST_CHECK_EQ(value.value(), http::protocol::v1_1);
#endif
}

void factory_boundary()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	http::client_config config;
	auto connector = std::make_shared<fake_connector>(executor);
	auto factory = std::make_shared<fake_factory>(executor);
	auto pool = std::make_shared<detail::session_pool>(
		executor, config, connector, factory);
	auto key = detail::make_session_key(
		make_head("http://example.test/factory"), config);

	bool completed = false;
	pool->open(key, make_head("http://example.test/factory"),
		http::body_spec::empty(),
		[&](riwo::error_code error, std::shared_ptr<detail::stream> active) {
			RIWO_TEST_CHECK(!error);
			RIWO_TEST_CHECK(active);
			completed = true;
		});
	drain(context);
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK_EQ(connector->connect_count, 1U);
	RIWO_TEST_CHECK_EQ(factory->sessions.size(), 1U);
	RIWO_TEST_CHECK_EQ(factory->protocols.at(0), http::protocol::v1_1);
	RIWO_TEST_CHECK(factory->keys.at(0) == key);

	auto missing = detail::make_default_session_factory()->create(key,
		detail::negotiated_transport {}, config);
	RIWO_TEST_CHECK_EQ(missing.error(),
		http::make_error_code(http::errc::negotiation_failed));
}

void capacity_and_fair_queue()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	http::client_config config;
	config.pool.max_sessions_per_origin = 1;
	auto connector = std::make_shared<fake_connector>(executor);
	auto factory = std::make_shared<fake_factory>(executor);
	auto pool = std::make_shared<detail::session_pool>(
		executor, config, connector, factory);
	auto key = detail::make_session_key(
		make_head("http://example.test/queue"), config);

	std::vector<std::string> completed;
	std::map<std::string, std::shared_ptr<detail::stream>> active;
	auto open = [&](std::string target) {
		pool->open(key, make_head("http://example.test" + target),
			http::body_spec::empty(),
			[&, target](riwo::error_code error,
				std::shared_ptr<detail::stream> stream) {
				RIWO_TEST_CHECK(!error);
				active[target] = std::move(stream);
				completed.push_back(target);
			});
	};

	open("/one");
	drain(context);
	RIWO_TEST_CHECK_EQ(completed.size(), 1U);
	RIWO_TEST_CHECK_EQ(factory->sessions.size(), 1U);
	auto first = factory->sessions.front();
	RIWO_TEST_CHECK(first->busy);

	open("/two");
	open("/three");
	drain_for(context, std::chrono::milliseconds(10));
	RIWO_TEST_CHECK_EQ(completed.size(), 1U);
	RIWO_TEST_CHECK_EQ(pool->waiter_count(), 2U);
	RIWO_TEST_CHECK_EQ(factory->sessions.size(), 1U);

	first->release_stream(true);
	drain_for(context, std::chrono::milliseconds(10));
	RIWO_TEST_CHECK_EQ(completed.size(), 2U);
	RIWO_TEST_CHECK_EQ(completed.at(1), "/two");
	RIWO_TEST_CHECK_EQ(factory->sessions.size(), 1U);
	RIWO_TEST_CHECK_EQ(first->opened_targets.size(), 2U);
	active.erase("/one");
	drain_for(context, std::chrono::milliseconds(1));
	RIWO_TEST_CHECK(first->busy);

	first->release_stream(true);
	drain_for(context, std::chrono::milliseconds(10));
	RIWO_TEST_CHECK_EQ(completed.size(), 3U);
	RIWO_TEST_CHECK_EQ(completed.at(2), "/three");
	RIWO_TEST_CHECK_EQ(first->opened_targets.back(), "/three");
	first->release_stream(true);
	RIWO_TEST_CHECK_EQ(pool->idle_session_count(), 1U);
}

void waiter_limits_timeout_and_draining()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	http::client_config config;
	config.pool.max_sessions_per_origin = 1;
	config.pool.max_waiters_per_origin = 1;
	config.timeout.pool_wait = std::chrono::milliseconds(10);
	auto connector = std::make_shared<fake_connector>(executor);
	auto factory = std::make_shared<fake_factory>(executor);
	auto pool = std::make_shared<detail::session_pool>(
		executor, config, connector, factory);
	auto key = detail::make_session_key(
		make_head("http://example.test/wait"), config);

	std::shared_ptr<detail::stream> active_stream;
	pool->open(key, make_head("http://example.test/active"),
		http::body_spec::empty(),
		[&](riwo::error_code, std::shared_ptr<detail::stream> stream) {
			active_stream = std::move(stream);
		});
	drain(context);
	auto active = factory->sessions.front();

	bool queued_completed = false;
	riwo::error_code queued_error;
	pool->open(key, make_head("http://example.test/waiting"),
		http::body_spec::empty(),
		[&](riwo::error_code error, std::shared_ptr<detail::stream>) {
			queued_completed = true;
			queued_error = error;
		});
	bool limited = false;
	pool->open(key, make_head("http://example.test/limited"),
		http::body_spec::empty(),
		[&](riwo::error_code error, std::shared_ptr<detail::stream>) {
			limited = error ==
				http::make_error_code(http::errc::buffer_limit_exceeded);
		});
	RIWO_TEST_CHECK(limited);

	drain(context);
	RIWO_TEST_CHECK(queued_completed);
	RIWO_TEST_CHECK_EQ(queued_error,
		asio::error::make_error_code(asio::error::timed_out));

	active->release_stream(false);
	drain(context);
	RIWO_TEST_CHECK_EQ(pool->session_count(), 0U);

	bool draining_failed = false;
	pool->cancel();
	pool->open(key, make_head("http://example.test/cancelled"),
		http::body_spec::empty(),
		[&](riwo::error_code error, std::shared_ptr<detail::stream>) {
			draining_failed = error ==
				asio::error::make_error_code(asio::error::operation_aborted);
		});
	drain(context);
	RIWO_TEST_CHECK(draining_failed);
}

void idle_expiry()
{
	riwo::io_context_t context;
	auto executor = asio::any_io_executor(context.get_executor());
	http::client_config config;
	config.pool.idle_timeout = std::chrono::milliseconds(5);
	auto connector = std::make_shared<fake_connector>(executor);
	auto factory = std::make_shared<fake_factory>(executor);
	auto pool = std::make_shared<detail::session_pool>(
		executor, config, connector, factory);
	auto key = detail::make_session_key(
		make_head("http://example.test/idle"), config);

	std::shared_ptr<detail::stream> active_stream;
	pool->open(key, make_head("http://example.test/idle"),
		http::body_spec::empty(),
		[&](riwo::error_code, std::shared_ptr<detail::stream> stream) {
			active_stream = std::move(stream);
		});
	drain(context);
	auto session = factory->sessions.front();
	session->release_stream(true);
	RIWO_TEST_CHECK_EQ(pool->idle_session_count(), 1U);

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	drain(context);
	RIWO_TEST_CHECK_EQ(pool->idle_session_count(), 0U);
	RIWO_TEST_CHECK_EQ(pool->session_count(), 0U);
}

void plaintext_preface_detector()
{
	http::transport::bounded_plaintext_preface_detector detector;
	auto preface = std::string(
		http::transport::bounded_plaintext_preface_detector::http2_preface);
	auto first = detector.feed(riwo::const_buffer(
		std::string_view(preface).substr(0, 7)));
	RIWO_TEST_CHECK(first == http::transport::preface_result::need_more);
	auto second = detector.feed(riwo::const_buffer(
		std::string_view(preface).substr(7)));
	RIWO_TEST_CHECK(second == http::transport::preface_result::http2);
	RIWO_TEST_CHECK_EQ(detector.buffer(), preface);

	http::transport::bounded_plaintext_preface_detector http1;
	const std::string request = "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n";
	auto result = http1.feed(riwo::const_buffer(request));
	RIWO_TEST_CHECK(result == http::transport::preface_result::http1);
	const auto inspected = request.substr(0,
		http::transport::bounded_plaintext_preface_detector::
			http2_preface.size());
	RIWO_TEST_CHECK_EQ(http1.buffer(), inspected);
	RIWO_TEST_CHECK_EQ(http1.take_buffer(), inspected);
	RIWO_TEST_CHECK(http1.buffer().empty());
}

void serve_keep_alive(asio::ip::tcp::socket socket, size_t &served,
	size_t expected)
{
	std::array<char,4096> buffer {};
	while( served < expected )
	{
		std::string input;
		riwo::error_code error;
		while( input.find("\r\n\r\n") == std::string::npos )
		{
			auto size = socket.read_some(asio::buffer(buffer), error);
			if( error ) return;
			input.append(buffer.data(), size);
		}
		const std::string response =
			"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
		asio::write(socket, asio::buffer(response), error);
		if( error ) return;
		++served;
	}
}

void h1_pool_reuses_connection()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::make_address("127.0.0.1"), 0});
	const auto port = acceptor.local_endpoint().port();
	size_t accepted = 0;
	size_t served = 0;
	std::thread server([&] {
		asio::ip::tcp::socket socket(context);
		riwo::error_code error;
		acceptor.accept(socket, error);
		if( error ) return;
		++accepted;
		serve_keep_alive(std::move(socket), served, 4);
	});

	http::client_config config;
	config.pool.max_sessions_per_origin = 1;
	config.pool.idle_timeout = std::chrono::milliseconds(0);
	http::client client(context, config);
	auto completed = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		const auto target = std::format("http://127.0.0.1:{}/reuse", port);
		for( size_t index = 0; index < 4; ++index )
		{
			auto reply = co_await client.fetch(
				http::request(http::method::get, target),
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
			RIWO_TEST_CHECK_EQ(std::string_view(
				reinterpret_cast<const char *>(reply.buffered_body().data()),
				reply.buffered_body().size()), "ok");
		}
		co_return;
	}, asio::use_future);
	context.run();
	completed.get();
	server.join();
	riwo::error_code ignored;
	acceptor.close(ignored);
	RIWO_TEST_CHECK_EQ(accepted, 1U);
	RIWO_TEST_CHECK_EQ(served, 4U);
}

void h1_server_keep_alive()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/value",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				std::string("value"), riwo::use_awaitable);
		})
		.start();
	const auto port = service.acceptor().local_endpoint().port();

	http::client_config config;
	config.pool.max_sessions_per_origin = 1;
	config.pool.idle_timeout = std::chrono::milliseconds(5);
	http::client client(context, config);
	auto completed = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		const auto target = std::format("http://127.0.0.1:{}/value", port);
		for( size_t index = 0; index < 4; ++index )
		{
			auto reply = co_await client.fetch(
				http::request(http::method::get, target),
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
			RIWO_TEST_CHECK_EQ(std::string_view(
				reinterpret_cast<const char *>(reply.buffered_body().data()),
				reply.buffered_body().size()), "value");
		}
		service.stop();
		co_return;
	}, asio::use_future);
	context.run();
	completed.get();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"session key identity", session_key_identity},
		{"protocol negotiation", protocol_negotiation},
		{"session factory boundary", factory_boundary},
		{"capacity and fair queue", capacity_and_fair_queue},
		{"waiter limits timeout and draining",
			waiter_limits_timeout_and_draining},
		{"idle expiry", idle_expiry},
		{"plaintext preface detector", plaintext_preface_detector},
		{"HTTP/1 pool reuses connection", h1_pool_reuses_connection},
		{"HTTP/1 server keep-alive", h1_server_keep_alive}
	});
}
