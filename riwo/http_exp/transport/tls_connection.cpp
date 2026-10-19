// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tls_connection.h"

#if RIWO_OPENSSL_SUPPORT

namespace riwo::http_exp::transport
{

tls_connection::tls_connection(tls_socket socket) :
	m_socket(std::move(socket)), m_executor(m_socket.get_executor())
{
}

tls_connection::~tls_connection()
{
	cancel();
}

auto tls_connection::get_executor() const noexcept -> executor_type {
	return m_executor;
}

void tls_connection::read_some(mutable_buffer buffer, io_handler handler)
{
	m_socket.async_read_some(buffer,
	[handler = std::move(handler)](error_code error, size_t size) mutable {
		std::move(handler)(error, size);
	});
}

void tls_connection::write(const_buffer buffer, io_handler handler)
{
	asio::async_write(m_socket, buffer,
	[handler = std::move(handler)](error_code error, size_t size) mutable {
		std::move(handler)(error, size);
	});
}

void tls_connection::close(close_handler handler)
{
	auto self = shared_from_this();
	asio::post(m_socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable {
		error_code ignored;
		if( self->m_open )
		{
			self->m_open = false;
			self->m_socket.shutdown(ignored);
			self->m_socket.next_layer().shutdown(
				asio::ip::tcp::socket::shutdown_both, ignored);
			self->m_socket.next_layer().close(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tls_connection::cancel() noexcept
{
	if( !m_open ) return;
	m_open = false;
	error_code ignored;
	m_socket.next_layer().cancel(ignored);
	m_socket.next_layer().shutdown(asio::ip::tcp::socket::shutdown_both,
		ignored);
	m_socket.next_layer().close(ignored);
}

bool tls_connection::is_open() const noexcept {
	return m_open && m_socket.next_layer().is_open();
}

size_t tls_connection::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		return m_socket.read_some(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		m_open = false;
		return 0;
	}
}

size_t tls_connection::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		return asio::write(m_socket, buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		m_open = false;
		return 0;
	}
}

auto tls_connection::remote_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_socket.next_layer().remote_endpoint(error);
}

auto tls_connection::local_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_socket.next_layer().local_endpoint(error);
}

} //namespace riwo::http_exp::transport

#endif //RIWO_OPENSSL_SUPPORT
