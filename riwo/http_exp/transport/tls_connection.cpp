// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tls_connection.h"

#if RIWO_OPENSSL_SUPPORT

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

void apply_socket_timeout(tls_socket &socket, int option,
	optional<std::chrono::milliseconds> value) noexcept
{
	const auto timeout = value ? socket_timeout(*value) : socket_timeout_type{};
	(void)::setsockopt(socket.next_layer().native_handle(), SOL_SOCKET,
		option, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
}

[[nodiscard]] bool timeout_error(const error_code &error) noexcept
{
	return error == asio::error::make_error_code(asio::error::would_block) or
		error == asio::error::make_error_code(asio::error::try_again);
}

} //namespace

class RIWO_DECL_HIDDEN tls_connection::impl
{
public:
	explicit impl(tls_socket value) :
		m_socket(std::move(value)), m_executor(m_socket.get_executor())
	{
	}

	tls_socket m_socket;
	bool m_open = true;
	executor_type m_executor;
	optional<std::chrono::milliseconds> m_read_timeout;
	optional<std::chrono::milliseconds> m_write_timeout;
};

tls_connection::tls_connection(tls_socket socket) :
	m_impl(std::make_unique<impl>(std::move(socket)))
{
}

tls_connection::~tls_connection()
{
	cancel();
}

auto tls_connection::get_executor() const noexcept -> executor_type {
	return m_impl->m_executor;
}

void tls_connection::read_some(mutable_buffer buffer, io_handler handler)
{
	if( not m_impl->m_read_timeout )
	{
		m_impl->m_socket.async_read_some(buffer,
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
	auto state = std::make_shared<timeout_state>(m_impl->m_executor);
	state->timer.expires_after(*m_impl->m_read_timeout);
	state->timer.async_wait([self, state](error_code error)
	{
		if( error or state->expired.exchange(true) ) return;
		self->cancel();
	});
	m_impl->m_socket.async_read_some(buffer,
	[self, state, handler = std::move(handler)]
	(error_code error, size_t size) mutable {
		(void)state->timer.cancel();
		if( state->expired and
			error == asio::error::make_error_code(
				asio::error::operation_aborted) )
			error = asio::error::make_error_code(asio::error::timed_out);
		std::move(handler)(error, size);
	});
}

void tls_connection::write(const_buffer buffer, io_handler handler)
{
	if( not m_impl->m_write_timeout )
	{
		asio::async_write(m_impl->m_socket, buffer,
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
	auto state = std::make_shared<timeout_state>(m_impl->m_executor);
	state->timer.expires_after(*m_impl->m_write_timeout);
	state->timer.async_wait([self, state](error_code error)
	{
		if( error or state->expired.exchange(true) ) return;
		self->cancel();
	});
	asio::async_write(m_impl->m_socket, buffer,
	[self, state, handler = std::move(handler)]
	(error_code error, size_t size) mutable {
		(void)state->timer.cancel();
		if( state->expired and
			error == asio::error::make_error_code(
				asio::error::operation_aborted) )
			error = asio::error::make_error_code(asio::error::timed_out);
		std::move(handler)(error, size);
	});
}

void tls_connection::close(close_handler handler)
{
	auto self = shared_from_this();
	asio::post(m_impl->m_socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable {
		error_code ignored;
		if( self->m_impl->m_open )
		{
			self->m_impl->m_open = false;
			self->m_impl->m_socket.shutdown(ignored);
			self->m_impl->m_socket.next_layer().shutdown(
				asio::ip::tcp::socket::shutdown_both, ignored);
			self->m_impl->m_socket.next_layer().close(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tls_connection::cancel() noexcept
{
	if( not m_impl->m_open ) return;
	m_impl->m_open = false;
	error_code ignored;
	m_impl->m_socket.next_layer().cancel(ignored);
	m_impl->m_socket.next_layer().shutdown(asio::ip::tcp::socket::shutdown_both,
		ignored);
	m_impl->m_socket.next_layer().close(ignored);
}

bool tls_connection::is_open() const noexcept {
	return m_impl->m_open and m_impl->m_socket.next_layer().is_open();
}

void tls_connection::set_read_timeout(optional<timeout_type> value)
{
	m_impl->m_read_timeout = value;
	apply_socket_timeout(m_impl->m_socket, SO_RCVTIMEO, value);
}

void tls_connection::set_write_timeout(optional<timeout_type> value)
{
	m_impl->m_write_timeout = value;
	apply_socket_timeout(m_impl->m_socket, SO_SNDTIMEO, value);
}

size_t tls_connection::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		std::atomic<bool> finished {false};
		std::atomic<bool> timed_out {false};
		std::jthread watchdog;
		if( m_impl->m_read_timeout )
		{
			const auto timeout = *m_impl->m_read_timeout;
			watchdog = std::jthread([&, timeout]
			{
				const auto deadline = std::chrono::steady_clock::now() + timeout;
				while( not finished and
					std::chrono::steady_clock::now() < deadline )
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				if( finished ) return;
				timed_out = true;
				error_code ignored;
				m_impl->m_socket.next_layer().close(ignored);
			});
		}
		auto size = m_impl->m_socket.read_some(buffer, error);
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
		m_impl->m_open = false;
		return 0;
	}
}

size_t tls_connection::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		std::atomic<bool> finished {false};
		std::atomic<bool> timed_out {false};
		std::jthread watchdog;
		if( m_impl->m_write_timeout )
		{
			const auto timeout = *m_impl->m_write_timeout;
			watchdog = std::jthread([&, timeout]
			{
				const auto deadline = std::chrono::steady_clock::now() + timeout;
				while( not finished and
					std::chrono::steady_clock::now() < deadline )
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				if( finished ) return;
				timed_out = true;
				error_code ignored;
				m_impl->m_socket.next_layer().close(ignored);
			});
		}
		auto size = asio::write(m_impl->m_socket, buffer, error);
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
		m_impl->m_open = false;
		return 0;
	}
}

auto tls_connection::remote_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_impl->m_socket.next_layer().remote_endpoint(error);
}

auto tls_connection::local_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_impl->m_socket.next_layer().local_endpoint(error);
}

tls_socket &tls_connection::socket() noexcept {
	return m_impl->m_socket;
}

} //namespace riwo::http_exp

#endif //RIWO_OPENSSL_SUPPORT
