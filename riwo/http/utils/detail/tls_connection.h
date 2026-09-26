// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_DETAIL_TLS_CONNECTION_H
#define RIWO_HTTP_UTILS_DETAIL_TLS_CONNECTION_H

#if RIWO_OPENSSL_SUPPORT

namespace riwo::http
{

template <core_concepts::exec Exec>
basic_tls_connection<Exec>::basic_tls_connection(socket_t &&socket) :
	m_socket(std::move(socket))
{

}

template <core_concepts::exec Exec>
basic_tls_connection<Exec>::~basic_tls_connection() = default;

template <core_concepts::exec Exec>
sys_expected<> basic_tls_connection<Exec>::cancel() noexcept
{
	auto &socket = m_socket.next_layer();
	if( not socket.is_open() )
		return make_sys_expected();
	error_code error {};
	socket.cancel(error);
	if( error )
		return sys_unexpected(error);
	return make_sys_expected();
}

template <core_concepts::exec Exec>
sys_expected<> basic_tls_connection<Exec>::close() noexcept
{
	// Do not perform SSL shutdown here: it may wait for the peer's close_notify.
	// A connection close is a fast abortive transport close by design.
	auto &socket = m_socket.next_layer();
	if( not socket.is_open() )
		return make_sys_expected();

	error_code ignored {};
	socket.cancel(ignored);
	socket.shutdown(asio::socket_base::shutdown_both, ignored);

	error_code error {};
	socket.close(error);
	if( error )
		return sys_unexpected(error);
	return make_sys_expected();
}

template <core_concepts::exec Exec>
sys_expected<> basic_tls_connection<Exec>::set_options(const tcp_socket_options &options) noexcept
{
	return detail::set_tcp_socket_options(m_socket.next_layer(), options);
}

template <core_concepts::exec Exec>
sys_expected<tcp_socket_state> basic_tls_connection<Exec>::options() const noexcept
{
	return detail::get_tcp_socket_options(m_socket.next_layer());
}

template <core_concepts::exec Exec>
bool basic_tls_connection<Exec>::is_open() const noexcept
{
	return m_socket.next_layer().is_open();
}

template <core_concepts::exec Exec>
auto basic_tls_connection<Exec>::probe() noexcept -> sys_expected<probe_state_t>
{
	if( (::SSL_get_shutdown(m_socket.native_handle()) & SSL_RECEIVED_SHUTDOWN) != 0 )
		return connection_probe_state::peer_closed;

	if( ::SSL_pending(m_socket.native_handle()) > 0 )
		return connection_probe_state::data_pending;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	if( ::SSL_has_pending(m_socket.native_handle()) > 0 )
		return connection_probe_state::data_pending;
#endif //OPENSSL_VERSION_NUMBER

	return detail::probe_tcp_socket(m_socket.next_layer());
}

template <core_concepts::exec Exec>
endpoint basic_tls_connection<Exec>::remote_endpoint() const noexcept
{
	error_code error {};
	auto value = m_socket.next_layer().remote_endpoint(error);
	return error ? endpoint{} : detail::to_endpoint(value);
}

template <core_concepts::exec Exec>
endpoint basic_tls_connection<Exec>::local_endpoint() const noexcept
{
	error_code error {};
	auto value = m_socket.next_layer().local_endpoint(error);
	return error ? endpoint{} : detail::to_endpoint(value);
}

template <core_concepts::exec Exec>
auto basic_tls_connection<Exec>::get_executor() noexcept -> executor_t
{
	return m_socket.get_executor();
}

template <core_concepts::exec Exec>
size_t basic_tls_connection<Exec>::read_some(mutable_buffer buffer, error_code &error) noexcept
{
	return m_socket.read_some(buffer, error);
}

template <core_concepts::exec Exec>
size_t basic_tls_connection<Exec>::write_all(const const_buffer &buffer, error_code &error) noexcept
{
	return asio::write(m_socket, buffer, error);
}

template <core_concepts::exec Exec>
size_t basic_tls_connection<Exec>::write_all(std::span<const const_buffer> buffers, error_code &error) noexcept
{
	return asio::write(m_socket, buffers, error);
}

template <core_concepts::exec Exec>
void basic_tls_connection<Exec>::co_read_some
(mutable_buffer buffer, io_handler_t handler) noexcept
{
	m_socket.async_read_some(buffer, std::move(handler));
}

template <core_concepts::exec Exec>
void basic_tls_connection<Exec>::co_write_all
(const_buffer buffer, io_handler_t handler) noexcept
{
	asio::async_write(m_socket, buffer, std::move(handler));
}

template <core_concepts::exec Exec>
void basic_tls_connection<Exec>::co_write_all
(std::span<const const_buffer> buffers, io_handler_t handler) noexcept
{
	detail::const_buffer_sequence sequence(buffers);
	asio::async_write(m_socket, sequence, std::move(handler));
}

} //namespace riwo::http


#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_HTTP_UTILS_DETAIL_TLS_CONNECTION_H
