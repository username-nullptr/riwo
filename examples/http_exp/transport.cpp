// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace
{

namespace http = riwo::http_exp;
using executor_t = riwo::io_context_t::executor_type;

struct scripted_state
{
	std::string response;
	std::string request;
	size_t offset = 0;
	bool open = true;
};

class scripted_stream
{
public:
	using executor_type = executor_t;

	scripted_stream(executor_t executor, std::shared_ptr<scripted_state> state) :
		m_state(std::move(state)), m_exec(std::move(executor)) {}

	[[nodiscard]] executor_t get_executor() const noexcept {
		return m_exec;
	}

	void read_some(const riwo::mutable_buffer &buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		if( not m_state->open or m_state->offset == m_state->response.size() )
		{
			std::move(handler)(asio::error::eof, 0);
			return;
		}
		const auto count = std::min (
			buffer.size(), m_state->response.size() - m_state->offset
		);
		std::memcpy(buffer.data(),
			m_state->response.data() + m_state->offset, count
		);
		m_state->offset += count;
		std::move(handler)(riwo::error_code{}, count);
	}

	void write(const riwo::const_buffer &buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		m_state->request.append (
			static_cast<const char*>(buffer.data()), buffer.size()
		);
		std::move(handler)(riwo::error_code{}, buffer.size());
	}

	void close(asio::any_completion_handler<void(riwo::error_code)> handler)
	{
		m_state->open = false;
		std::move(handler)(riwo::error_code{});
	}

	void cancel() noexcept {
		m_state->open = false;
	}
	[[nodiscard]] bool is_open() const noexcept {
		return m_state->open;
	}

private:
	executor_t m_exec;
	std::shared_ptr<scripted_state> m_state;
};

static_assert(http::concepts::byte_stream_model<scripted_stream>);

class scripted_connector final : public http::basic_transport_connector<executor_t>
{
public:
	scripted_connector(executor_t executor, std::shared_ptr<scripted_state> state) :
		m_exec(std::move(executor)), m_state(std::move(state)) {}

	[[nodiscard]] executor_t get_executor() const noexcept override {
		return m_exec;
	}
	void connect(http::connect_request, connect_handler_t handler) override
	{
		asio::post(m_exec,
		[executor = m_exec, state = m_state, handler = std::move(handler)]() mutable
		{
			state->open = true;
			state->offset = 0;

			std::move(handler)(riwo::error_code{}, result_t
			{
				http::make_byte_stream(scripted_stream(executor, std::move(state))),
				http::protocol::v1_1, {}, {}
			});
		});
	}

	void cancel() noexcept override {
		m_state->open = false;
	}

private:
	std::shared_ptr<scripted_state> m_state {};
	executor_t m_exec;
};

} //namespace

int main()
{
	auto state = std::make_shared<scripted_state>();
	state->response =
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 24\r\n"
		"Connection: close\r\n\r\n"
		"served without a socket\n";

	auto connector = std::make_shared
		<scripted_connector>(riwo::get_executor(), state);

	http::basic_client<executor_t> client(connector);
	int result = 0;

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		try {
			auto reply = co_await client.get (
				"http://memory.example/", riwo::use_awaitable
			);
			auto body = co_await reply.read_all(riwo::use_awaitable);

			std::cout << http::example::body_text(body)
				<< "Captured request:\n" << state->request;
		}
		catch(const std::exception &exception)
		{
			std::cerr << "Custom transport failed: "
				<< exception.what() << '\n';
			result = 1;
		}
	});
	riwo::io_context().run();
	return result;
}
