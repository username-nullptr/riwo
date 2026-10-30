// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_IMPL_TRANSPORT_IPP
#define RIWO_WEBSOCKET_EXP_DETAIL_STREAM_IMPL_TRANSPORT_IPP

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_IMPL_H
#error "Include <riwo/websocket_exp/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket_exp
{

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::read_transport
(const mutable_buffer &buffer, error_code &error) noexcept
{
	return m_connection->read_some(buffer, error);
}

template <core_concepts::exec Exec>
awaitable<std::tuple<error_code,size_t>> basic_stream<Exec>::impl::async_read_transport
(std::shared_ptr<std::vector<std::byte>> storage)
{
	return m_stream_transport.async_read(std::move(storage));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::start_transport_write
(const prepared_frame &frame, detail::wire_frame_kind kind, detail::send_operation_ptr operation) noexcept
{
	m_stream_transport.start_write(send_owner(), frame, kind, operation);
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
		m_connection->cancel();
	m_connection->close(error);
}

} //namespace riwo::websocket_exp


#endif //RIWO_WEBSOCKET_EXP_DETAIL_STREAM_IMPL_TRANSPORT_IPP
