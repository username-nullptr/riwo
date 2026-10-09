// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/protocol/cookie.h>
#include <riwo/http/protocol/types.h>
#include <riwo/http/protocol/version.h>
#include <riwo/http/client.h>
#include <riwo/http/server/response.h>
#include <riwo/core/system/app_utls.h>

namespace
{

class scoped_environment
{
	struct entry
	{
		std::string name;
		riwo::optional<riwo::app::path_t> value;
	};

public:
	scoped_environment(std::initializer_list<std::string_view> names)
	{
		for(const auto name : names)
		{
			auto value = riwo::app::getenv(name);
			m_entries.push_back({std::string(name), value ?
				riwo::optional<riwo::app::path_t>(*value) : riwo::nullopt});
		}
	}

	~scoped_environment()
	{
		for(const auto &item : m_entries)
		{
			if( item.value )
				riwo::ignore_unused(riwo::app::setenv(item.name, *item.value));
			else
				riwo::ignore_unused(riwo::app::unsetenv(item.name));
		}
	}

private:
	std::vector<entry> m_entries;
};

class scripted_connection final : public riwo::http::basic_connection<>
{
public:
	struct io_step
	{
		size_t transferred = 0;
		riwo::error_code error {};
		bool wait_for_cancellation = false;
	};
	struct read_step
	{
		std::string payload {};
		riwo::error_code error {};
		size_t offset = 0;
	};

	explicit scripted_connection(executor_t exec) : m_exec(std::move(exec)) {}

	void push_write(size_t transferred, riwo::error_code error = {})
	{
		m_writes.push_back({transferred, error});
	}

	void push_cancellable_write(size_t transferred)
	{
		m_writes.push_back({transferred, {}, true});
	}

	void push_read(std::string payload, riwo::error_code error = {})
	{
		m_reads.push_back({std::move(payload), error});
	}

	void set_probe_state(riwo::sys_expected<probe_state_t> state)
	{
		m_probe_state = std::move(state);
	}

	riwo::sys_expected<> cancel() noexcept override {
		return riwo::make_sys_expected();
	}

	riwo::sys_expected<> close() noexcept override {
		m_open = false;
		return riwo::make_sys_expected();
	}

	riwo::sys_expected<> set_options(
		const riwo::http::tcp_socket_options&
	) noexcept override {
		return riwo::make_sys_expected();
	}

	riwo::sys_expected<riwo::http::tcp_socket_state>
	options() const noexcept override {
		return riwo::http::tcp_socket_state {};
	}

	bool is_open() const noexcept override {
		return m_open;
	}

	riwo::sys_expected<probe_state_t> probe() noexcept override {
		return m_probe_state;
	}

	riwo::http::endpoint remote_endpoint() const noexcept override {
		return {};
	}

	riwo::http::endpoint local_endpoint() const noexcept override {
		return {};
	}

	executor_t get_executor() noexcept override {
		return m_exec;
	}

	[[nodiscard]] const std::string &written_data() const noexcept {
		return m_written_data;
	}

protected:
	using riwo::http::basic_connection<>::write_all;
	using riwo::http::basic_connection<>::co_write_all;

	size_t read_some(
		riwo::mutable_buffer buffer, riwo::error_code &error
	) noexcept override
	{
		return read_into(buffer, error);
	}

	size_t write_all(
		const riwo::const_buffer &buffer, riwo::error_code &error
	) noexcept override
	{
		auto step = next_write(buffer.size());
		m_written_data.append(static_cast<const char*>(buffer.data()),
			step.transferred);
		error = step.error;
		return step.transferred;
	}

	void co_read_some(
		riwo::mutable_buffer buffer, io_handler_t completion
	) noexcept override
	{
		riwo::error_code error {};
		const auto size = read_into(buffer, error);
		asio::post(m_exec,
			[handler = std::move(completion), error, size]() mutable {
				std::move(handler)(error, size);
			});
	}

	void co_write_all(
		riwo::const_buffer buffer, io_handler_t completion
	) noexcept override
	{
		auto step = next_write(buffer.size());
		m_written_data.append(static_cast<const char*>(buffer.data()),
			step.transferred);
		if( step.wait_for_cancellation )
		{
			auto slot = asio::get_associated_cancellation_slot(completion);
			if( not slot.is_connected() )
			{
				asio::post(m_exec,
					[handler = std::move(completion)]() mutable {
						std::move(handler)(
							std::make_error_code(std::errc::operation_not_supported), 0
						);
					});
				return ;
			}
			struct pending_write
			{
				std::optional<io_handler_t> handler;
			};
			auto pending = std::make_shared<pending_write>(
				pending_write {std::move(completion)}
			);
			slot.assign(
				[pending, step](asio::cancellation_type type) mutable
				{
					if( type == asio::cancellation_type::none or
						not pending->handler )
						return ;
					auto pending_completion = std::move(*pending->handler);
					pending->handler.reset();
					std::move(pending_completion)(
						asio::error::operation_aborted, step.transferred
					);
				});
			return ;
		}
		asio::post(m_exec,
			[handler = std::move(completion), step]() mutable {
				std::move(handler)(step.error, step.transferred);
			});
	}

private:
	[[nodiscard]] size_t read_into(
		riwo::mutable_buffer buffer, riwo::error_code &error
	) noexcept
	{
		if( m_reads.empty() )
		{
			error = asio::error::eof;
			return 0;
		}

		auto &front = m_reads.front();
		const auto remaining = front.payload.size() - front.offset;
		const auto size = std::min(remaining, buffer.size());
		if( size > 0 )
		{
			std::memcpy(buffer.data(), front.payload.data() + front.offset, size);
			front.offset += size;
		}
		if( front.offset == front.payload.size() )
		{
			error = front.error;
			m_reads.pop_front();
		}
		return size;
	}

	[[nodiscard]] io_step next_write(size_t requested) noexcept
	{
		if( m_writes.empty() )
			return {requested, {}};
		auto step = m_writes.front();
		m_writes.pop_front();
		step.transferred = std::min(step.transferred, requested);
		return step;
	}

	executor_t m_exec;
	std::deque<read_step> m_reads {};
	std::deque<io_step> m_writes {};
	riwo::sys_expected<probe_state_t> m_probe_state {probe_state_t::no_event};
	std::string m_written_data {};
	bool m_open = true;
};

class scripted_connector final : public riwo::http::connector
{
public:
	explicit scripted_connector(executor_t exec) :
		riwo::http::connector(std::move(exec)) {}

	[[nodiscard]] size_t connection_count(std::string_view host) const
	{
		auto pos = m_connection_counts.find(std::string(host));
		return pos == m_connection_counts.end() ? 0 : pos->second;
	}

	void push_response(std::string response)
	{
		m_responses.emplace_back(std::move(response));
	}

	[[nodiscard]] std::shared_ptr<scripted_connection> last_connection() const
	{
		return m_connections.empty() ? nullptr : m_connections.back();
	}

	[[nodiscard]] const riwo::http::connect_target &last_target() const
	{
		return m_targets.back();
	}

protected:
	riwo::sys_expected<connection_ptr> do_connect(
		const riwo::http::connect_target &target
	) noexcept override
	{
		riwo::error_code error {};
		try {
			m_targets.emplace_back(target);
			++m_connection_counts[target.host];
			auto connection = std::make_shared<scripted_connection>(get_executor());
			if( not m_responses.empty() )
			{
				connection->push_read(std::move(m_responses.front()));
				m_responses.pop_front();
			}
			m_connections.emplace_back(connection);
			return connection_ptr(std::move(connection));
		}
		catch(const std::bad_alloc&) {
			error = std::make_error_code(std::errc::not_enough_memory);
		}
		catch(...) {
			error = std::make_error_code(std::errc::io_error);
		}
		return riwo::sys_unexpected(error);
	}

	riwo::awaitable<riwo::sys_expected<connection_ptr>> co_do_connect(
		const riwo::http::connect_target &target
	) noexcept override
	{
		co_return do_connect(target);
	}

private:
	std::unordered_map<std::string,size_t> m_connection_counts {};
	std::deque<std::string> m_responses {};
	std::vector<std::shared_ptr<scripted_connection>> m_connections {};
	std::vector<riwo::http::connect_target> m_targets {};
};

void protocol_enums()
{
	using namespace riwo::http;

	RIWO_TEST_CHECK_EQ(method::from_string("GET"), method::get);
	RIWO_TEST_CHECK_EQ(std::string(method::string(method::delet)), "DELETE");
	RIWO_TEST_CHECK_EQ(
		std::string(status::description(status::not_found)), "Not Found"
	);
	RIWO_TEST_CHECK_EQ(version::from_string("1.1"), version::v11);
	RIWO_TEST_CHECK_EQ(std::string(version::string(version::v10)), "1.0");
	RIWO_TEST_CHECK_EQ(version(version::v11).number(), 1.1);

	constexpr methods safe_methods {method::get, method::head};
	static_assert(safe_methods.test_flag(method::get));
	static_assert(safe_methods.test_flag(method::head));
	static_assert(not safe_methods.test_flag(method::post));
}

void cookie_values()
{
	riwo::http::cookie item("session-token");
	item.set_domain("example.test")
		.set_path("/account")
		.set_same_site("Strict")
		.set_priority("High")
		.set_expires(3600)
		.set_max_age(1800)
		.set_size(13)
		.set_http_only(true)
		.set_secure(true);

	RIWO_TEST_CHECK_EQ(item.value<std::string>(), "session-token");
	RIWO_TEST_CHECK_EQ(*item.domain(), "example.test");
	RIWO_TEST_CHECK_EQ(*item.path(), "/account");
	RIWO_TEST_CHECK_EQ(*item.same_site(), "Strict");
	RIWO_TEST_CHECK_EQ(*item.priority(), "High");
	RIWO_TEST_CHECK_EQ(*item.expires(), std::uint64_t {3600});
	RIWO_TEST_CHECK_EQ(*item.max_age(), std::uint64_t {1800});
	RIWO_TEST_CHECK_EQ(*item.size(), size_t {13});
	RIWO_TEST_CHECK(*item.http_only());
	RIWO_TEST_CHECK(*item.secure());

	item.unset_domain().unset_http_only().unset_secure();
	RIWO_TEST_CHECK(not item.domain());
	RIWO_TEST_CHECK(not item.http_only());
	RIWO_TEST_CHECK(not item.secure());

	riwo::http::cookie copied(item);
	copied.set_path("/other");
	RIWO_TEST_CHECK_EQ(*item.path(), "/account");
	RIWO_TEST_CHECK_EQ(*copied.path(), "/other");
}

void client_url_validation()
{
	riwo::io_context_t context;
	riwo::http::client client(context.get_executor());
	std::error_code error;

	auto fragmented = client.request_get(
		riwo::url("http://example.test/path#fragment"), error
	);
	RIWO_TEST_CHECK(not fragmented);
	RIWO_TEST_CHECK(error == std::errc::invalid_argument);

	error.clear();
	auto wrong_scheme = client.request_get(
		riwo::url("ws://example.test/path"), error
	);
	RIWO_TEST_CHECK(not wrong_scheme);
	RIWO_TEST_CHECK(error == std::errc::protocol_not_supported);

	error.clear();
	auto missing_host = client.request_get(
		riwo::url("http:///path"), error
	);
	RIWO_TEST_CHECK(not missing_host);
	RIWO_TEST_CHECK(error == std::errc::invalid_argument);
}

void client_proxy_inheritance_and_environment()
{
	using namespace riwo::http;
	const scoped_environment environment({
		"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY",
		"ws_proxy", "WS_PROXY", "wss_proxy", "WSS_PROXY",
		"all_proxy", "ALL_PROXY", "no_proxy", "NO_PROXY",
	});
	RIWO_TEST_CHECK(riwo::app::setenv("http_proxy",
		"http://user:secret@proxy.test:8080/"));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("HTTP_PROXY"));
#endif
	RIWO_TEST_CHECK(riwo::app::setenv("https_proxy",
		"http://secure-proxy.test:8443/"));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("HTTPS_PROXY"));
#endif
	RIWO_TEST_CHECK(riwo::app::unsetenv("all_proxy"));
	RIWO_TEST_CHECK(riwo::app::unsetenv("ALL_PROXY"));
	RIWO_TEST_CHECK(riwo::app::setenv("ws_proxy",
		"http://websocket-only.test:8082/"));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("WS_PROXY"));
#endif
	RIWO_TEST_CHECK(riwo::app::setenv("wss_proxy",
		"http://secure-websocket-only.test:8444/"));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("WSS_PROXY"));
#endif
	RIWO_TEST_CHECK(riwo::app::setenv("no_proxy",
		".bypass.test,127.0.0.0/8"));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("NO_PROXY"));
#endif

	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(context.get_executor());
	connection_pool pool(connector);
	client requester(std::move(pool));
	riwo::error_code error;

	{
		auto request = requester.request_get(
			"http://origin.test/resource?q=1", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "proxy.test");
		RIWO_TEST_CHECK_EQ(connector->last_target().port, 8080);
		RIWO_TEST_CHECK(not connector->last_target().tunnel);
		const auto &wire = connector->last_connection()->written_data();
		RIWO_TEST_CHECK(wire.starts_with(
			"GET http://origin.test/resource?q=1 HTTP/1.1\r\n"));
		RIWO_TEST_CHECK(wire.find(
			"Proxy-Authorization: Basic dXNlcjpzZWNyZXQ=\r\n") !=
			std::string::npos);
	}

	{
		auto request = requester.make_get("https://secure.test/resource", error);
		RIWO_TEST_CHECK(request and not error);
		const auto &target = connector->last_target();
		RIWO_TEST_CHECK_EQ(target.host, "secure.test");
		RIWO_TEST_CHECK_EQ(target.port, 443);
		RIWO_TEST_CHECK(target.tunnel);
		RIWO_TEST_CHECK_EQ(target.tunnel->type,
			proxy_tunnel_type::http_connect);
		RIWO_TEST_CHECK_EQ(target.tunnel->host, "secure-proxy.test");
		RIWO_TEST_CHECK_EQ(target.tunnel->port, 8443);
	}

	{
		auto request = requester.make_get("http://api.bypass.test/value", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "api.bypass.test");
		RIWO_TEST_CHECK(not connector->last_target().tunnel);
	}

	{
		auto request = requester.make_get("http://127.12.34.56/value", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "127.12.34.56");
	}

	{
		client::req_info request_info("http://direct.test/value");
		request_info.proxy = no_proxy;
		auto request = requester.make_get(std::move(request_info), error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "direct.test");
	}

	{
		client::req_info request_info("http://override-origin.test/value");
		request_info.proxy = riwo::url("http://override-proxy.test:9000/");
		auto request = requester.make_get(std::move(request_info), error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host,
			"override-proxy.test");
		RIWO_TEST_CHECK_EQ(connector->last_target().port, 9000);
	}

	RIWO_TEST_CHECK(riwo::app::unsetenv("http_proxy"));
	RIWO_TEST_CHECK(riwo::app::setenv("HTTP_PROXY",
		"http://uppercase-proxy.test:8081/"));
	{
		auto request = requester.make_get("http://uppercase-origin.test/", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host,
			"uppercase-proxy.test");
		RIWO_TEST_CHECK_EQ(connector->last_target().port, 8081);
	}

	RIWO_TEST_CHECK(riwo::app::unsetenv("HTTP_PROXY"));
	{
		auto request = requester.make_get("http://http-only.test/", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "http-only.test");
		RIWO_TEST_CHECK(not connector->last_target().tunnel);
	}

	RIWO_TEST_CHECK(riwo::app::unsetenv("https_proxy"));
	RIWO_TEST_CHECK(riwo::app::unsetenv("HTTPS_PROXY"));
	{
		auto request = requester.make_get("https://https-only.test/", error);
		RIWO_TEST_CHECK(request and not error);
		RIWO_TEST_CHECK_EQ(connector->last_target().host, "https-only.test");
		RIWO_TEST_CHECK(not connector->last_target().tunnel);
	}

	RIWO_TEST_CHECK(riwo::app::setenv("all_proxy",
		"socks5h://socks-proxy.test:1081/"));
	{
		auto request = requester.make_get("http://fallback-origin.test/", error);
		RIWO_TEST_CHECK(request and not error);
		const auto &target = connector->last_target();
		RIWO_TEST_CHECK_EQ(target.host, "fallback-origin.test");
		RIWO_TEST_CHECK(target.tunnel);
		RIWO_TEST_CHECK_EQ(target.tunnel->type, proxy_tunnel_type::socks5);
		RIWO_TEST_CHECK_EQ(target.tunnel->host, "socks-proxy.test");
		RIWO_TEST_CHECK_EQ(target.tunnel->port, 1081);
	}
}

void connection_partial_write_counts()
{
	riwo::io_context_t context;
	auto connection = std::make_shared<scripted_connection>(
		context.get_executor()
	);
	const std::string payload = "abcdef";
	const riwo::const_buffer body(payload.data(), payload.size());
	const auto failure = std::make_error_code(std::errc::broken_pipe);

	connection->push_write(3, failure);
	riwo::error_code error {};
	auto size = connection->write(body, error);
	RIWO_TEST_CHECK_EQ(size, size_t {3});
	RIWO_TEST_CHECK_EQ(error, failure);

	error = failure;
	size = connection->write(body, error);
	RIWO_TEST_CHECK_EQ(size, payload.size());
	RIWO_TEST_CHECK(not error);

	connection->push_write(2, failure);
	auto expected = connection->write(body);
	RIWO_TEST_CHECK(not expected.has_value());
	RIWO_TEST_CHECK_EQ(expected.error(), failure);

	const std::string first = "ab";
	const std::string second = "cde";
	const std::array<riwo::const_buffer,2> buffers {{
		{first.data(), first.size()},
		{second.data(), second.size()},
	}};
	connection->push_write(first.size());
	connection->push_write(1, failure);
	error.clear();
	size = connection->write(std::span<const riwo::const_buffer>(buffers), error);
	RIWO_TEST_CHECK_EQ(size, first.size() + size_t {1});
	RIWO_TEST_CHECK_EQ(error, failure);

	connection->push_write(4, failure);
	bool async_completed = false;
	connection->write(body,
		[&](riwo::error_code async_error, size_t transferred)
		{
			RIWO_TEST_CHECK_EQ(transferred, size_t {4});
			RIWO_TEST_CHECK_EQ(async_error, failure);
			async_completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(async_completed);

	context.restart();
	connection->push_write(first.size());
	connection->push_write(2, failure);
	bool async_sequence_completed = false;
	connection->write(std::span<const riwo::const_buffer>(buffers),
		[&](riwo::error_code async_error, size_t transferred)
		{
			RIWO_TEST_CHECK_EQ(transferred, first.size() + size_t {2});
			RIWO_TEST_CHECK_EQ(async_error, failure);
			async_sequence_completed = true;
		});
	context.run();
	RIWO_TEST_CHECK(async_sequence_completed);

	context.restart();
	connection->push_write(5, failure);
	bool timed_completed = false;
	using namespace riwo::operators;
	connection->write(body,
		([&](riwo::error_code async_error, size_t transferred)
		{
			RIWO_TEST_CHECK_EQ(transferred, size_t {5});
			RIWO_TEST_CHECK_EQ(async_error, failure);
			timed_completed = true;
		}) | std::chrono::seconds(1));
	context.run();
	RIWO_TEST_CHECK(timed_completed);

	context.restart();
	connection->push_cancellable_write(2);
	bool timeout_completed = false;
	connection->write(body,
		([&](riwo::error_code async_error, size_t transferred)
		{
			RIWO_TEST_CHECK_EQ(transferred, size_t {2});
			RIWO_TEST_CHECK_EQ(async_error,
				asio::error::make_error_code(asio::error::timed_out));
			timeout_completed = true;
		}) | std::chrono::milliseconds(1));
	context.run();
	RIWO_TEST_CHECK(timeout_completed);
}

void http_body_write_counts()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	const std::string payload = "abcdef";
	const auto failure = std::make_error_code(std::errc::broken_pipe);

	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(3, failure);
		auto lease = std::make_shared<connection_lease>(
			connection, [](connection_lease::connection_ptr) {}
		);
		request_context<method::post> request(
			std::move(lease), riwo::url("http://example.test/upload")
		);

		riwo::error_code error {};
		auto bytes = request.write(riwo::buffer(payload), error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {3});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(5, failure); // "6\r\n" plus two body bytes.
		auto lease = std::make_shared<connection_lease>(
			connection, [](connection_lease::connection_ptr) {}
		);
		request_context<method::post> request(
			std::move(lease), riwo::url("http://example.test/upload")
		);
		request.set_header(header::transfer_encoding, "chunked");

		riwo::error_code error {};
		auto bytes = request.write(riwo::buffer(payload), error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {2});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(4, failure);
		response reply(connection);

		riwo::error_code error {};
		auto bytes = reply.write(riwo::buffer(payload), error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {4});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(6, failure); // "6\r\n" plus three body bytes.
		response reply(connection);
		reply.set_header(header::transfer_encoding, "chunked");

		riwo::error_code error {};
		auto bytes = reply.write(riwo::buffer(payload), error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {3});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(5, failure);
		auto reply = std::make_shared<response>(connection);
		bool completed = false;
		asio::co_spawn(context,
			[&, reply]() -> riwo::awaitable<void>
			{
				auto [error, bytes] = co_await reply->write (
					riwo::buffer(payload), asio::as_tuple(riwo::use_awaitable)
				);
				RIWO_TEST_CHECK_EQ(bytes, size_t {5});
				RIWO_TEST_CHECK_EQ(error, failure);
				completed = true;
				co_return;
			}, asio::detached);
		context.run();
		RIWO_TEST_CHECK(completed);
	}
	{
		context.restart();
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(4, failure);
		auto reply = std::make_shared<response>(connection);
		bool completed = false;
		using namespace riwo::operators;
		asio::co_spawn(context,
			[&, reply]() -> riwo::awaitable<void>
			{
				auto [error, bytes] = co_await reply->write (
					riwo::buffer(payload),
					asio::as_tuple(riwo::use_awaitable) |
						std::chrono::seconds(1)
				);
				RIWO_TEST_CHECK_EQ(bytes, size_t {4});
				RIWO_TEST_CHECK_EQ(error, failure);
				completed = true;
				co_return;
			}, asio::detached);
		context.run();
		RIWO_TEST_CHECK(completed);
	}
	{
		context.restart();
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_cancellable_write(2);
		auto reply = std::make_shared<response>(connection);
		bool completed = false;
		using namespace riwo::operators;
		asio::co_spawn(context,
			[&, reply]() -> riwo::awaitable<void>
			{
				auto [error, bytes] = co_await reply->write (
					riwo::buffer(payload),
					asio::as_tuple(riwo::use_awaitable) |
						std::chrono::milliseconds(1)
				);
				RIWO_TEST_CHECK_EQ(bytes, size_t {2});
				RIWO_TEST_CHECK_EQ(error,
					asio::error::make_error_code(asio::error::timed_out));
				completed = true;
				co_return;
			}, asio::detached);
		context.run();
		RIWO_TEST_CHECK(completed);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		auto lease = std::make_shared<connection_lease>(
			connection, [](connection_lease::connection_ptr) {}
		);
		request_context<method::post> request(
			std::move(lease), riwo::url("http://example.test/chunked")
		);
		request.set_header(header::transfer_encoding, "chunked");
		riwo::error_code error {};
		RIWO_TEST_CHECK_EQ(request.write(riwo::buffer(payload), error), payload.size());
		RIWO_TEST_CHECK(not error);
		RIWO_TEST_CHECK_EQ(request.chunk_end(error), size_t {0});
		RIWO_TEST_CHECK(not error);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		response reply(connection);
		reply.set_header(header::transfer_encoding, "chunked");
		riwo::error_code error {};
		RIWO_TEST_CHECK_EQ(reply.write(riwo::buffer(payload), error), payload.size());
		RIWO_TEST_CHECK(not error);
		RIWO_TEST_CHECK_EQ(reply.chunk_end(error), size_t {0});
		RIWO_TEST_CHECK(not error);
	}
}

void http_file_body_write_counts()
{
	using namespace riwo::http;
	riwo::test::temporary_directory directory;
	const auto file = directory.path() / "payload.txt";
	const std::string payload = "file-body";
	{
		std::ofstream stream(file, std::ios::binary);
		stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
	}

	riwo::io_context_t context;
	const auto failure = std::make_error_code(std::errc::broken_pipe);
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		response reply(connection);
		riwo::error_code error {};
		auto bytes = reply.send_file(file, error);
		RIWO_TEST_CHECK_EQ(bytes, payload.size());
		RIWO_TEST_CHECK(not error);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(4, failure);
		response reply(connection);
		riwo::error_code error {};
		auto bytes = reply.send_file(file, error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {4});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		auto lease = std::make_shared<connection_lease>(
			connection, [](connection_lease::connection_ptr) {}
		);
		request_context<method::put> request(
			std::move(lease), riwo::url("http://example.test/upload")
		);
		riwo::error_code error {};
		auto bytes = request.upload_file(basic_body_norms{}, file, error);
		RIWO_TEST_CHECK_EQ(bytes, payload.size());
		RIWO_TEST_CHECK(not error);
	}
	{
		auto connection = std::make_shared<scripted_connection>(
			context.get_executor()
		);
		connection->push_write(std::numeric_limits<size_t>::max());
		connection->push_write(3, failure);
		auto lease = std::make_shared<connection_lease>(
			connection, [](connection_lease::connection_ptr) {}
		);
		request_context<method::put> request(
			std::move(lease), riwo::url("http://example.test/upload")
		);
		riwo::error_code error {};
		auto bytes = request.upload_file(basic_body_norms{}, file, error);
		RIWO_TEST_CHECK_EQ(bytes, size_t {3});
		RIWO_TEST_CHECK_EQ(error, failure);
	}
}

void connection_pool_indexed_lru()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(context.get_executor());
	connection_pool_config config;
	config.max_count = 2;
	config.timeout.idle = std::chrono::seconds(60);
	connection_pool pool(connector, config);

	const connect_target first {"first.test", 80, security_mode::plain};
	const connect_target second {"second.test", 80, security_mode::plain};
	const connect_target third {"third.test", 80, security_mode::plain};

	auto acquire_and_release = [&](const connect_target &target)
	{
		auto lease = pool.get(target);
		RIWO_TEST_CHECK(lease);
		RIWO_TEST_CHECK(*lease);
		(*lease)->release();
	};

	acquire_and_release(first);
	acquire_and_release(second);
	RIWO_TEST_CHECK_EQ(pool.count(), 2U);

	// At capacity, a new target evicts the globally oldest idle connection.
	acquire_and_release(third);
	RIWO_TEST_CHECK_EQ(connector->connection_count("first.test"), 1U);
	RIWO_TEST_CHECK_EQ(connector->connection_count("second.test"), 1U);
	RIWO_TEST_CHECK_EQ(connector->connection_count("third.test"), 1U);
	RIWO_TEST_CHECK_EQ(pool.count(), 2U);

	// first.test was evicted, while third.test remains reusable by exact key.
	acquire_and_release(first);
	RIWO_TEST_CHECK_EQ(connector->connection_count("first.test"), 2U);
	acquire_and_release(third);
	RIWO_TEST_CHECK_EQ(connector->connection_count("third.test"), 1U);
	RIWO_TEST_CHECK_EQ(pool.count(), 2U);
}

void connection_pool_indexed_waiters()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(context.get_executor());
	connection_pool_config config;
	config.max_count = 1;
	connection_pool pool(connector, config);
	const connect_target first {"first.test", 80, security_mode::plain};
	const connect_target second {"second.test", 80, security_mode::plain};

	auto held_result = pool.get(first);
	RIWO_TEST_CHECK(held_result);
	auto held = *held_result;
	std::vector<std::string> acquisition_order;

	auto second_waiter = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		auto lease = co_await pool.get(second, riwo::use_awaitable);
		acquisition_order.emplace_back("second");
		lease->release();
	}, asio::use_future);
	auto matching_waiter = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		auto lease = co_await pool.get(first, riwo::use_awaitable);
		acquisition_order.emplace_back("first");
		lease->release();
	}, asio::use_future);

	// Both waiters are enqueued before the held connection is returned.
	asio::post(context, [held] { held->release(); });
	context.run();
	second_waiter.get();
	matching_waiter.get();
	RIWO_TEST_CHECK_EQ(acquisition_order.size(), 2U);
	RIWO_TEST_CHECK_EQ(acquisition_order[0], "first");
	RIWO_TEST_CHECK_EQ(acquisition_order[1], "second");
}

void connection_pool_discards_failed_connections()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(context.get_executor());
	connection_pool pool(connector);
	const connect_target target {"failed.test", 80, security_mode::plain};

	auto first_result = pool.get(target);
	RIWO_TEST_CHECK(first_result);
	auto first = *first_result;
	auto failed_connection = connector->last_connection();
	RIWO_TEST_CHECK(failed_connection);
	failed_connection->set_probe_state(connection_probe_state::peer_closed);
	first->release();

	RIWO_TEST_CHECK(not failed_connection->is_open());
	RIWO_TEST_CHECK_EQ(pool.count(), 0U);

	auto second_result = pool.get(target);
	RIWO_TEST_CHECK(second_result);
	RIWO_TEST_CHECK_EQ(connector->connection_count("failed.test"), 2U);
	(*second_result)->release();
}

void http_client_does_not_reuse_malformed_reply()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(context.get_executor());
	connector->push_response(
		"HTTP/1.1 200 OK\r\nMissing-Colon\r\n\r\n");
	connector->push_response(
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	connection_pool pool(connector);
	client requester(std::move(pool), {.default_proxy = no_proxy});

	riwo::error_code error {};
	auto malformed = requester.request_get(
		"http://malformed.test/first", error);
	RIWO_TEST_CHECK(malformed);
	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(malformed->wait_reply(error), status::none);
	RIWO_TEST_CHECK(error);
	RIWO_TEST_CHECK(not malformed->reply()->lease().is_valid());

	error.clear();
	auto valid = requester.request_get("http://malformed.test/second", error);
	RIWO_TEST_CHECK(valid);
	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(valid->wait_reply(error), status::ok);
	RIWO_TEST_CHECK(not error);
	RIWO_TEST_CHECK_EQ(connector->connection_count("malformed.test"), 2U);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"protocol enums", protocol_enums},
		{"cookie values", cookie_values},
		{"client URL validation", client_url_validation},
		{"client proxy inheritance and environment",
			client_proxy_inheritance_and_environment},
		{"connection partial write counts", connection_partial_write_counts},
		{"HTTP body write counts", http_body_write_counts},
		{"HTTP file body write counts", http_file_body_write_counts},
		{"connection pool indexed LRU", connection_pool_indexed_lru},
		{"connection pool indexed waiters", connection_pool_indexed_waiters},
		{"connection pool discards failed connections",
			connection_pool_discards_failed_connections},
		{"HTTP client does not reuse malformed replies",
			http_client_does_not_reuse_malformed_reply},
	});
}
