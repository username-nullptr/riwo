// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tcp_connection.h"
#include "detail/buffer_sequence.h"
#include "detail/timeout.h"

namespace riwo::http_exp { namespace
{

struct RIWO_DECL_HIDDEN operation final : std::enable_shared_from_this<operation>
{
	using close_handler_t = connection::close_handler_t;

	tcp_connection_ptr owner {};
	asio::ip::tcp::socket *socket = nullptr;

	std::string host {};
	uint16_t port = 0;

	close_handler_t handler {};
	asio::ip::tcp::resolver resolver;

	operation(std::shared_ptr<tcp_connection> owner_value,
		std::string host_value, uint16_t port_value, close_handler_t completion) :
		owner(std::move(owner_value)), socket(&owner->socket()),
		host(std::move(host_value)), port(port_value),
		handler(std::move(completion)),
		resolver(owner->socket().get_executor()) {}

	void start()
	{
		auto self = shared_from_this();
		resolver.async_resolve(host, std::to_string(port), [self = std::move(self)]
		(error_code error, asio::ip::tcp::resolver::results_type endpoints) mutable
		{
			if( error )
				return self->finish(error);

			asio::async_connect(*self->socket, endpoints, [self = std::move(self)]
			(error_code connect_error, const asio::ip::tcp::endpoint&) mutable {
				self->finish(connect_error);
			});
		});
	}

	void finish(error_code error) {
		std::move(handler)(error);
	}
};

} //namespace

class RIWO_DECL_HIDDEN tcp_connection::impl
{
public:
	explicit impl(asio::any_io_executor executor) :
		m_socket(executor), m_exec(std::move(executor)) {}

	explicit impl(asio::ip::tcp::socket value) :
		m_socket(std::move(value)), m_exec(m_socket.get_executor()) {}

	asio::ip::tcp::socket m_socket;
	executor_t m_exec {};

	optional<std::chrono::milliseconds> m_read_timeout {};
	optional<std::chrono::milliseconds> m_write_timeout {};
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

void tcp_connection::connect_sync(std::string_view host, uint16_t port, error_code &error) noexcept
{
	error.clear();
	try {
		asio::ip::tcp::resolver resolver(m_impl->m_socket.get_executor());
		auto endpoints = resolver.resolve(host, std::to_string(port), error);

		if( not error )
			asio::connect(m_impl->m_socket, endpoints, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
}

void tcp_connection::connect_async(std::string_view host, uint16_t port, close_handler_t handler)
{
	std::make_shared<operation>(shared_from_this(),
		std::string(host), port, std::move(handler)
	)->start();
}

void tcp_connection::read_some(mutable_buffer buffer, io_handler_t handler)
{
	if( not m_impl->m_read_timeout )
	{
		m_impl->m_socket.async_read_some(buffer, std::move(handler));
		return ;
	}
	detail::start_timed_io(shared_from_this(),
		m_impl->m_exec, *m_impl->m_read_timeout, std::move(handler),
		[socket = &m_impl->m_socket, buffer](auto completion) mutable {
			socket->async_read_some(buffer, std::move(completion));
		}
	);
}

void tcp_connection::write(const_buffer buffer, io_handler_t handler)
{
	if( not m_impl->m_write_timeout )
	{
		asio::async_write(m_impl->m_socket, buffer, std::move(handler));
		return ;
	}
	detail::start_timed_io(shared_from_this(),
		m_impl->m_exec, *m_impl->m_write_timeout, std::move(handler),
		[socket = &m_impl->m_socket, buffer](auto completion) mutable {
			asio::async_write(*socket, buffer, std::move(completion));
		}
	);
}

size_t tcp_connection::read_some_sync(const mutable_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	try {
		return detail::run_timed_sync_io(m_impl->m_read_timeout,
			[&]{
				return m_impl->m_socket.read_some(buffer, error);
			},
			[&]{
				error_code ignored;
				ignored = m_impl->m_socket.shutdown (
					asio::ip::tcp::socket::shutdown_both, ignored
				);
				RIWO_UNUSED(ignored);
			},
			error
		);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	return 0;
}

size_t tcp_connection::write_sync(const const_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	try {
		return detail::run_timed_sync_io(m_impl->m_write_timeout,
			[&] {
				return asio::write(m_impl->m_socket, buffer, error);
			},
			[&] {
				error_code ignored;
				ignored = m_impl->m_socket.shutdown (
					asio::ip::tcp::socket::shutdown_both, ignored
				);
				RIWO_UNUSED(ignored);
			},
			error
		);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	return 0;
}

void tcp_connection::close(close_handler_t handler)
{
	auto self = shared_from_this();
	asio::post(m_impl->m_socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable
	{
		if( self->m_impl->m_socket.is_open() )
		{
			error_code ignored;
			ignored = self->m_impl->m_socket.shutdown (
				asio::ip::tcp::socket::shutdown_both, ignored
			);
			ignored = self->m_impl->m_socket.close(ignored);
			RIWO_UNUSED(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tcp_connection::cancel() noexcept
{
	if( m_impl->m_socket.is_open() )
	{
		error_code ignored;
		ignored = m_impl->m_socket.cancel(ignored);
		ignored = m_impl->m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		ignored = m_impl->m_socket.close(ignored);
		RIWO_UNUSED(ignored);
	}
}

void tcp_connection::set_read_timeout(optional<timeout_t> value)
{
	m_impl->m_read_timeout = value;
	detail::apply_socket_timeout(m_impl->m_socket, SO_RCVTIMEO, value);
}

void tcp_connection::set_write_timeout(optional<timeout_t> value)
{
	m_impl->m_write_timeout = value;
	detail::apply_socket_timeout(m_impl->m_socket, SO_SNDTIMEO, value);
}

auto tcp_connection::get_executor() const noexcept -> executor_t
{
	return m_impl->m_exec;
}

bool tcp_connection::is_open() const noexcept
{
	return m_impl->m_socket.is_open();
}

auto tcp_connection::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error.clear();
	return m_impl->m_socket.remote_endpoint(error);
}

auto tcp_connection::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error.clear();
	return m_impl->m_socket.local_endpoint(error);
}

asio::ip::tcp::socket &tcp_connection::socket() noexcept
{
	return m_impl->m_socket;
}

void tcp_connection::write_span(std::span<const const_buffer> buffers, io_handler_t handler)
{
	detail::const_buffer_sequence sequence(buffers);
	if( not m_impl->m_write_timeout )
	{
		asio::async_write(m_impl->m_socket, sequence, std::move(handler));
		return ;
	}
	detail::start_timed_io(shared_from_this(),
		m_impl->m_exec, *m_impl->m_write_timeout, std::move(handler),
		[socket = &m_impl->m_socket, sequence = std::move(sequence)]
		(auto completion) mutable
		{
			asio::async_write(*socket,
				std::move(sequence), std::move(completion));
		}
	);
}

} //namespace riwo::http_exp
