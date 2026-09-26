// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_TRANSPORT_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_TRANSPORT_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
#error "Include <riwo/websocket/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket
{

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::read_transport
(const mutable_buffer &buffer, error_code &error) noexcept
{
	return m_connection->read(buffer, error);
}

template <core_concepts::exec Exec>
awaitable<std::tuple<error_code,size_t>> basic_stream<Exec>::impl::async_read_transport
(std::shared_ptr<std::vector<std::byte>> storage)
{
	return m_stream_transport.async_read(std::move(storage));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::start_transport_write
(prepared_frame frame, detail::wire_frame_kind kind, std::shared_ptr<detail::send_operation> operation) noexcept
{
	m_stream_transport.start_write(send_owner(), std::move(frame), kind,
		std::move(operation));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::start_async_transport_read
(void *connection, const mutable_buffer &buffer, io_handler_t handler)
{
	static_cast<connection_t*>(connection)->read(buffer, std::move(handler));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::start_async_transport_write
(void *connection, std::span<const const_buffer> buffers, io_handler_t handler)
{
	static_cast<connection_t*>(connection)->write(buffers, std::move(handler));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::close_transport(error_code &error, bool cancel_first) noexcept
{
	if( not m_connection or m_transport_closed )
	{
		error.clear();
		return ;
	}
	m_transport_closed = true;
	if( cancel_first )
		ignore_unused(m_connection->cancel());

	auto result = m_connection->close();
	error = result ? error_code{} : result.error();
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_TRANSPORT_IPP
