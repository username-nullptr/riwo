// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tcp_connection.h"
#include <asio/steady_timer.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace riwo::http_exp
{

namespace
{

#if defined(_WIN32)
using socket_timeout_type = DWORD;
#else
using socket_timeout_type = timeval;
#endif

[[nodiscard]] socket_timeout_type socket_timeout(
	const std::chrono::milliseconds &value)
{
#if defined(_WIN32)
	return static_cast<DWORD>(std::max<int64_t>(0, value.count()));
#else
	timeval result {};
	result.tv_sec = static_cast<time_t>(value.count() / 1000);
	result.tv_usec = static_cast<suseconds_t>((value.count() % 1000) * 1000);
	return result;
#endif
}

void apply_socket_timeout(asio::ip::tcp::socket &socket, int option,
	std::optional<std::chrono::milliseconds> value) noexcept
{
	error_code error;
	const auto timeout = value ? socket_timeout(*value) : socket_timeout_type{};
	(void)::setsockopt(socket.native_handle(), SOL_SOCKET, option,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	ignore_unused(error);
}

[[nodiscard]] bool timeout_error(const error_code &error) noexcept
{
	return error == asio::error::make_error_code(asio::error::would_block) ||
		error == asio::error::make_error_code(asio::error::try_again);
}

} //namespace

class RIWO_DECL_HIDDEN tcp_connection::impl
{
public:
	explicit impl(asio::any_io_executor executor) :
		socket(executor), executor(std::move(executor))
	{
	}

	explicit impl(asio::ip::tcp::socket value) :
		socket(std::move(value)), executor(socket.get_executor())
	{
	}

	asio::ip::tcp::socket socket;
	executor_type executor;
	optional<std::chrono::milliseconds> read_timeout;
	optional<std::chrono::milliseconds> write_timeout;
};

tcp_connection::tcp_connection(asio::any_io_executor executor) :
	m_impl(std::make_unique<impl>(std::move(executor)))
{
}

tcp_connection::tcp_connection(asio::ip::tcp::socket socket) :
	m_impl(std::make_unique<impl>(std::move(socket)))
{
}

tcp_connection::~tcp_connection() = default;

auto tcp_connection::get_executor() const noexcept -> executor_type {
	return m_impl->executor;
}

void tcp_connection::read_some(mutable_buffer buffer, io_handler handler)
{
	if( !m_impl->read_timeout )
	{
		m_impl->socket.async_read_some(buffer,
		[handler = std::move(handler)](error_code error, size_t size) mutable {
			std::move(handler)(error, size);
		});
		return;
	}

	struct timeout_state
	{
		explicit timeout_state(asio::any_io_executor executor) :
			timer(std::move(executor))
		{
		}

		asio::steady_timer timer;
		std::atomic<bool> expired {false};
	};
	auto self = shared_from_this();
	auto state = std::make_shared<timeout_state>(m_impl->executor);
	state->timer.expires_after(*m_impl->read_timeout);
	state->timer.async_wait([self, state](error_code error)
	{
		if( error || state->expired.exchange(true) ) return;
		self->cancel();
	});
	m_impl->socket.async_read_some(buffer,
	[self, state, handler = std::move(handler)]
	(error_code error, size_t size) mutable {
		(void)state->timer.cancel();
		if( state->expired &&
			error == asio::error::make_error_code(
				asio::error::operation_aborted) )
			error = asio::error::make_error_code(asio::error::timed_out);
		std::move(handler)(error, size);
	});
}

void tcp_connection::write(const_buffer buffer, io_handler handler)
{
	if( !m_impl->write_timeout )
	{
		asio::async_write(m_impl->socket, buffer,
		[handler = std::move(handler)](error_code error, size_t size) mutable {
			std::move(handler)(error, size);
		});
		return;
	}

	struct timeout_state
	{
		explicit timeout_state(asio::any_io_executor executor) :
			timer(std::move(executor))
		{
		}

		asio::steady_timer timer;
		std::atomic<bool> expired {false};
	};
	auto self = shared_from_this();
	auto state = std::make_shared<timeout_state>(m_impl->executor);
	state->timer.expires_after(*m_impl->write_timeout);
	state->timer.async_wait([self, state](error_code error)
	{
		if( error || state->expired.exchange(true) ) return;
		self->cancel();
	});
	asio::async_write(m_impl->socket, buffer,
	[self, state, handler = std::move(handler)]
	(error_code error, size_t size) mutable {
		(void)state->timer.cancel();
		if( state->expired &&
			error == asio::error::make_error_code(
				asio::error::operation_aborted) )
			error = asio::error::make_error_code(asio::error::timed_out);
		std::move(handler)(error, size);
	});
}

void tcp_connection::close(close_handler handler)
{
	auto self = shared_from_this();
	asio::post(m_impl->socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable {
		error_code ignored;
		if( self->m_impl->socket.is_open() )
		{
			self->m_impl->socket.shutdown(
				asio::ip::tcp::socket::shutdown_both, ignored);
			self->m_impl->socket.close(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tcp_connection::cancel() noexcept
{
	error_code ignored;
	if( m_impl->socket.is_open() )
	{
		m_impl->socket.cancel(ignored);
		m_impl->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		m_impl->socket.close(ignored);
	}
}

void tcp_connection::set_read_timeout(optional<timeout_type> value)
{
	m_impl->read_timeout = value;
	apply_socket_timeout(m_impl->socket, SO_RCVTIMEO, value);
}

void tcp_connection::set_write_timeout(optional<timeout_type> value)
{
	m_impl->write_timeout = value;
	apply_socket_timeout(m_impl->socket, SO_SNDTIMEO, value);
}

bool tcp_connection::is_open() const noexcept {
	return m_impl->socket.is_open();
}

size_t tcp_connection::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		std::atomic<bool> finished {false};
		std::atomic<bool> timed_out {false};
		std::jthread watchdog;
		if( m_impl->read_timeout )
		{
			const auto timeout = *m_impl->read_timeout;
			watchdog = std::jthread([&, timeout]
			{
				const auto deadline = std::chrono::steady_clock::now() + timeout;
				while( !finished &&
					std::chrono::steady_clock::now() < deadline )
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				if( finished ) return;
				timed_out = true;
				error_code ignored;
				m_impl->socket.close(ignored);
			});
		}
		auto size = m_impl->socket.read_some(buffer, error);
		finished = true;
		if( watchdog.joinable() ) watchdog.request_stop();
		if( timed_out )
			error = asio::error::make_error_code(asio::error::timed_out);
		if( timeout_error(error) )
			error = asio::error::make_error_code(asio::error::timed_out);
		return size;
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

size_t tcp_connection::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		std::atomic<bool> finished {false};
		std::atomic<bool> timed_out {false};
		std::jthread watchdog;
		if( m_impl->write_timeout )
		{
			const auto timeout = *m_impl->write_timeout;
			watchdog = std::jthread([&, timeout]
			{
				const auto deadline = std::chrono::steady_clock::now() + timeout;
				while( !finished &&
					std::chrono::steady_clock::now() < deadline )
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				if( finished ) return;
				timed_out = true;
				error_code ignored;
				m_impl->socket.close(ignored);
			});
		}
		auto size = asio::write(m_impl->socket, buffer, error);
		finished = true;
		if( watchdog.joinable() ) watchdog.request_stop();
		if( timed_out )
			error = asio::error::make_error_code(asio::error::timed_out);
		if( timeout_error(error) )
			error = asio::error::make_error_code(asio::error::timed_out);
		return size;
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void tcp_connection::connect_sync(std::string_view host, uint16_t port,
	error_code &error) noexcept
{
	error.clear();
	try {
		asio::ip::tcp::resolver resolver(m_impl->socket.get_executor());
		auto endpoints = resolver.resolve(host, std::to_string(port), error);
		if( error ) return;
		asio::connect(m_impl->socket, endpoints, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
}

void tcp_connection::connect_async(std::string_view host, uint16_t port,
	close_handler handler)
{
	struct operation final : std::enable_shared_from_this<operation>
	{
		std::shared_ptr<tcp_connection> owner;
		asio::ip::tcp::socket *socket;
		std::string host;
		uint16_t port;
		close_handler handler;
		asio::ip::tcp::resolver resolver;

		operation(std::shared_ptr<tcp_connection> owner_value,
			std::string host_value,
			uint16_t port_value, close_handler completion) :
			owner(std::move(owner_value)), socket(&owner->socket()),
			host(std::move(host_value)), port(port_value),
			handler(std::move(completion)),
			resolver(owner->socket().get_executor())
		{
		}

		void start()
		{
			auto self = shared_from_this();
			resolver.async_resolve(host, std::to_string(port),
			[self = std::move(self)](error_code error,
				asio::ip::tcp::resolver::results_type endpoints) mutable {
				if( error ) return self->finish(error);
				asio::async_connect(*self->socket, endpoints,
				[self = std::move(self)](error_code connect_error,
					const asio::ip::tcp::endpoint&) mutable {
					self->finish(connect_error);
				});
			});
		}

		void finish(error_code error) {
			std::move(handler)(error);
		}
	};

	std::make_shared<operation>(shared_from_this(), std::string(host), port,
		std::move(handler))->start();
}

auto tcp_connection::remote_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_impl->socket.remote_endpoint(error);
}

auto tcp_connection::local_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_impl->socket.local_endpoint(error);
}

asio::ip::tcp::socket &tcp_connection::socket() noexcept {
	return m_impl->socket;
}

} //namespace riwo::http_exp
