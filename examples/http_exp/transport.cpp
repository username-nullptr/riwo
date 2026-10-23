// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

namespace
{

namespace http = riwo::http_exp;
using executor_type = riwo::io_context_t::executor_type;

struct scripted_state
{
	std::string response;
	std::string request;
	size_t read_offset = 0;
	bool open = true;
};

class scripted_stream
{
public:
	using executor_type = riwo::io_context_t::executor_type;

	scripted_stream(executor_type executor, std::shared_ptr<scripted_state> state) :
		m_executor(std::move(executor)), m_state(std::move(state))
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept
	{
		return m_executor;
	}

	void read_some(riwo::mutable_buffer buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		if( not m_state->open or
			m_state->read_offset == m_state->response.size() )
		{
			std::move(handler)(
				asio::error::make_error_code(asio::error::eof), 0);
			return;
		}
		const auto count = std::min(buffer.size(),
			m_state->response.size() - m_state->read_offset);
		std::memcpy(buffer.data(),
			m_state->response.data() + m_state->read_offset, count);
		m_state->read_offset += count;
		std::move(handler)(riwo::error_code{}, count);
	}

	void write(riwo::const_buffer buffer,
		asio::any_completion_handler<void(riwo::error_code,size_t)> handler)
	{
		if( not m_state->open )
		{
			std::move(handler)(
				asio::error::make_error_code(asio::error::broken_pipe), 0);
			return;
		}
		m_state->request.append(
			static_cast<const char*>(buffer.data()), buffer.size());
		std::move(handler)(riwo::error_code{}, buffer.size());
	}

	[[nodiscard]] size_t read_some_sync(
		riwo::mutable_buffer buffer, riwo::error_code &error) noexcept
	{
		error.clear();
		if( not m_state->open or
			m_state->read_offset == m_state->response.size() )
		{
			error = asio::error::make_error_code(asio::error::eof);
			return 0;
		}
		const auto count = std::min(buffer.size(),
			m_state->response.size() - m_state->read_offset);
		std::memcpy(buffer.data(),
			m_state->response.data() + m_state->read_offset, count);
		m_state->read_offset += count;
		return count;
	}

	[[nodiscard]] size_t write_sync(
		riwo::const_buffer buffer, riwo::error_code &error) noexcept
	{
		error.clear();
		if( not m_state->open )
		{
			error = asio::error::make_error_code(asio::error::broken_pipe);
			return 0;
		}
		m_state->request.append(
			static_cast<const char*>(buffer.data()), buffer.size());
		return buffer.size();
	}

	void close(asio::any_completion_handler<void(riwo::error_code)> handler)
	{
		m_state->open = false;
		std::move(handler)(riwo::error_code{});
	}

	void cancel() noexcept
	{
		m_state->open = false;
	}

	[[nodiscard]] bool is_open() const noexcept
	{
		return m_state->open;
	}

private:
	executor_type m_executor;
	std::shared_ptr<scripted_state> m_state;
};

static_assert(http::concepts::byte_stream_model<scripted_stream>);

class scripted_connector final :
	public http::basic_transport_connector<executor_type>
{
public:
	scripted_connector(
		executor_type executor, std::shared_ptr<scripted_state> state) :
		m_executor(std::move(executor)), m_state(std::move(state))
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override
	{
		return m_executor;
	}

	[[nodiscard]] riwo::sys_expected<result_t> connect_sync(
		const http::connect_request &) noexcept override
	{
		m_state->open = true;
		m_state->read_offset = 0;
		return result_t {
			http::make_byte_stream(scripted_stream(m_executor, m_state)),
			http::protocol::v1_1, {}, {}
		};
	}

	void connect(http::connect_request,
		connect_handler_t handler) override
	{
		asio::post(m_executor,
			[executor = m_executor, state = m_state,
			 handler = std::move(handler)]() mutable
			{
				state->open = true;
				state->read_offset = 0;
				std::move(handler)(riwo::error_code{}, result_t {
					http::make_byte_stream(
						scripted_stream(executor, std::move(state))),
					http::protocol::v1_1, {}, {}
				});
			});
	}

	void cancel() noexcept override
	{
		m_state->open = false;
	}

private:
	executor_type m_executor;
	std::shared_ptr<scripted_state> m_state;
};

} //namespace

int main()
try
{
	auto state = std::make_shared<scripted_state>();
	state->response =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 24\r\n"
		"Connection: close\r\n"
		"\r\n"
		"served without a socket\n";

	riwo::io_context_t context;
	auto connector = std::make_shared<scripted_connector>(
		context.get_executor(), state);
	http::client_config config;
	config.protocols.requirement = http::protocol_requirement::require_http1;
	http::basic_client<executor_type> client(
		context.get_executor(), connector, config);

	http::request request(http::method::get,
		"http://memory.example/custom-transport");
	request.headers().add("Accept", "text/plain");
	auto reply = client.fetch(std::move(request));
	const auto body = reply.buffered_body();
	std::cout << "Custom transport selected "
		<< (reply.selected_protocol() == http::protocol::v1_1 ?
			"HTTP/1.1" : "another protocol") << '\n'
		<< "Response status: " << reply.status().code() << '\n'
		<< "Response body: " << std::string_view(
			reinterpret_cast<const char*>(body.data()), body.size())
		<< "Captured request:\n" << state->request;
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP transport example failed: "
		<< exception.what() << '\n';
	return 1;
}
