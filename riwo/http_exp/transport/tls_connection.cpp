// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tls_connection.h"

#if RIWO_OPENSSL_SUPPORT
#include "detail/buffer_sequence.h"
#include "detail/timeout.h"

namespace riwo::http_exp
{

class RIWO_DECL_HIDDEN tls_connection::impl
{
public:
	explicit impl(tls_socket_t value) :
		m_socket(std::move(value)), m_exec(m_socket.get_executor()) {}

	tls_socket_t m_socket;
	executor_t m_exec {};
	bool m_open = true;

	optional<std::chrono::milliseconds> m_read_timeout {};
	optional<std::chrono::milliseconds> m_write_timeout {};
};

tls_connection::tls_connection(tls_socket_t socket) :
	m_impl(std::make_unique<impl>(std::move(socket)))
{

}

tls_connection::~tls_connection()
{
	cancel();
}

void tls_connection::read_some(mutable_buffer buffer, io_handler_t handler)
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

void tls_connection::write(const_buffer buffer, io_handler_t handler)
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

size_t tls_connection::read_some_sync(const mutable_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	try {
		return detail::run_timed_sync_io(m_impl->m_read_timeout,
			[&]{
				return m_impl->m_socket.read_some(buffer, error);
			},
			[&]{
				error_code ignored;
				ignored = m_impl->m_socket.next_layer().shutdown(
					asio::ip::tcp::socket::shutdown_both, ignored
				);
				RIWO_UNUSED(ignored);
			},
			error
		);
	}
	catch(...)
	{
		error = exception_error(std::current_exception());
		m_impl->m_open = false;
	}
	return 0;
}

size_t tls_connection::write_sync(const const_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	try {
		return detail::run_timed_sync_io(m_impl->m_write_timeout,
			[&]{
				return asio::write(m_impl->m_socket, buffer, error);
			},
			[&]{
				error_code ignored;
				ignored = m_impl->m_socket.next_layer().shutdown(
					asio::ip::tcp::socket::shutdown_both, ignored
				);
				RIWO_UNUSED(ignored);
			},
			error
		);
	}
	catch(...)
	{
		error = exception_error(std::current_exception());
		m_impl->m_open = false;
	}
	return 0;
}

void tls_connection::close(close_handler_t handler)
{
	auto self = shared_from_this();
	asio::post(m_impl->m_socket.get_executor(),
	[self = std::move(self), handler = std::move(handler)]() mutable
	{
		if( self->m_impl->m_open )
		{
			error_code ignored;
			self->m_impl->m_open = false;

			ignored = self->m_impl->m_socket.shutdown(ignored);
			ignored = self->m_impl->m_socket.next_layer().shutdown (
				asio::ip::tcp::socket::shutdown_both, ignored
			);
			ignored = self->m_impl->m_socket.next_layer().close(ignored);
			RIWO_UNUSED(ignored);
		}
		std::move(handler)(error_code{});
	});
}

void tls_connection::cancel() noexcept
{
	if( not m_impl->m_open )
		return ;

	m_impl->m_open = false;
	error_code ignored;

	ignored = m_impl->m_socket.next_layer().cancel(ignored);
	ignored = m_impl->m_socket.next_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
	ignored = m_impl->m_socket.next_layer().close(ignored);
	RIWO_UNUSED(ignored);
}

void tls_connection::set_read_timeout(optional<timeout_t> value)
{
	m_impl->m_read_timeout = value;
	detail::apply_socket_timeout (
		m_impl->m_socket.next_layer(), SO_RCVTIMEO, value
	);
}

void tls_connection::set_write_timeout(optional<timeout_t> value)
{
	m_impl->m_write_timeout = value;
	detail::apply_socket_timeout (
		m_impl->m_socket.next_layer(), SO_SNDTIMEO, value
	);
}

auto tls_connection::get_executor() const noexcept -> executor_t
{
	return m_impl->m_exec;
}

bool tls_connection::is_open() const noexcept
{
	return m_impl->m_open and m_impl->m_socket.next_layer().is_open();
}

auto tls_connection::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error.clear();
	return m_impl->m_socket.next_layer().remote_endpoint(error);
}

auto tls_connection::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error.clear();
	return m_impl->m_socket.next_layer().local_endpoint(error);
}

tls_socket_t &tls_connection::socket() noexcept
{
	return m_impl->m_socket;
}

void tls_connection::write_span(std::span<const const_buffer> buffers, io_handler_t handler)
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

#endif //RIWO_OPENSSL_SUPPORT
