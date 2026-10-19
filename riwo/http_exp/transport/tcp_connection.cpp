// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tcp_connection.h"

namespace riwo::http_exp::transport
{

tcp_connection::tcp_connection(asio::any_io_executor executor) :
	m_socket(executor), m_executor(std::move(executor))
{
}

tcp_connection::tcp_connection(asio::ip::tcp::socket socket) :
	m_socket(std::move(socket)), m_executor(m_socket.get_executor())
{
}

auto tcp_connection::get_executor() const noexcept -> executor_type {
	return m_executor;
}

void tcp_connection::read_some(mutable_buffer buffer, io_handler handler)
{
	m_socket.async_read_some(buffer,
	[handler = std::move(handler)](error_code error, size_t size) mutable {
		std::move(handler)(error, size);
	});
}

void tcp_connection::write(const_buffer buffer, io_handler handler)
{
	asio::async_write(m_socket, buffer,
	[handler = std::move(handler)](error_code error, size_t size) mutable {
		std::move(handler)(error, size);
	});
}

void tcp_connection::close(close_handler handler)
{
	auto self = shared_from_this();
	asio::post(m_socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable {
		error_code ignored;
		if( self->m_socket.is_open() )
		{
			self->m_socket.shutdown(
				asio::ip::tcp::socket::shutdown_both, ignored);
			self->m_socket.close(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tcp_connection::cancel() noexcept
{
	error_code ignored;
	if( m_socket.is_open() )
	{
		m_socket.cancel(ignored);
		m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		m_socket.close(ignored);
	}
}

bool tcp_connection::is_open() const noexcept {
	return m_socket.is_open();
}

size_t tcp_connection::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try {
		return m_socket.read_some(buffer, error);
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
		return asio::write(m_socket, buffer, error);
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
		asio::ip::tcp::resolver resolver(m_socket.get_executor());
		auto endpoints = resolver.resolve(host, std::to_string(port), error);
		if( error ) return;
		asio::connect(m_socket, endpoints, error);
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
			owner(std::move(owner_value)), socket(&owner->m_socket),
			host(std::move(host_value)), port(port_value),
			handler(std::move(completion)),
			resolver(owner->m_socket.get_executor())
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
	return m_socket.remote_endpoint(error);
}

auto tcp_connection::local_endpoint(error_code &error) const noexcept
	-> endpoint_type
{
	error.clear();
	return m_socket.local_endpoint(error);
}

} //namespace riwo::http_exp::transport
