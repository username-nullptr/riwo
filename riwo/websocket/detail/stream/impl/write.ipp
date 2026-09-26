// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_WRITE_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_WRITE_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
# error "Include <riwo/websocket/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket
{

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::write
(message_type type, std::span<const const_buffer> buffers, write_options options, error_code &error) noexcept
{
	error.clear();
	if( m_state == connection_state::idle )
	{
		error = make_error_code(errc::not_open);
		return 0;
	}
	if( m_state == connection_state::closing )
	{
		error = make_error_code(errc::closing);
		return 0;
	}
	if( m_state == connection_state::closed )
	{
		error = make_error_code(errc::closed);
		return 0;
	}
	if( m_state == connection_state::failed )
	{
		error = m_error ? m_error : make_system_error_code(std::errc::io_error);
		return 0;
	}
	if( type != message_type::text and type != message_type::binary )
	{
		error = make_system_error_code(std::errc::invalid_argument);
		return 0;
	}
	if( m_send_engine.busy() )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return 0;
	}
	auto prepared = m_send_engine.prepare_message(type, buffers, options);
	if( not prepared )
	{
		error = prepared.error();
		return 0;
	}
	size_t body_transferred = 0;
	for(const auto &frame : *prepared)
	{
		const auto wire_transferred = write_prepared(frame, error);
		if( error )
		{
			if( frame.application_size == frame.payload_size )
				body_transferred += wire_transferred;
			fail(error);
			return body_transferred;
		}
		body_transferred += frame.application_size;
	}
	error.clear();
	return body_transferred;
}

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::write_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin, error_code &error) noexcept
{
	return m_send_engine.write_data_frame (
		type, payload, continuation, fin, error
	);
}

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::write_prepared
(const prepared_frame &frame, error_code &error) noexcept
{
	if( m_send_engine.busy() )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return 0;
	}
	const auto wire_size = m_connection->write (
		std::span(frame.buffers), error
	);
	const auto payload_size = wire_size > frame.header_size ?
		std::min(frame.payload_size, wire_size - frame.header_size) : 0;

	if( not error and wire_size != frame.header_size + frame.payload_size )
		error = make_system_error_code(std::errc::io_error);
	return payload_size;
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_write_message(message_type type, std::span<const const_buffer> buffers,
	write_options options, std::shared_ptr<std::vector<std::byte>> payload_owner,
	io_handler_t handler)
{
	m_send_engine.async_write_message(type, buffers, options,
		std::move(payload_owner), std::move(handler)
	);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_write_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin,
 std::shared_ptr<std::vector<std::byte>> payload_owner, io_handler_t handler)
{
	m_send_engine.async_write_data_frame(type, payload, continuation, fin,
		std::move(payload_owner), std::move(handler)
	);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_write_control
(opcode op, const const_buffer &payload, io_handler_t handler, bool automatic)
{
	if( not automatic and automatic_control_enabled() )
	{
		post_completion(m_exec, std::move(handler),
			make_system_error_code(std::errc::operation_not_permitted), size_t{0}
		);
		return ;
	}
	m_send_engine.async_write_control(op, payload, std::move(handler));
}

template <core_concepts::exec Exec>
size_t basic_stream<Exec>::impl::write_control
(opcode op, const const_buffer &payload, error_code &error, bool automatic) noexcept
{
	if( not automatic and automatic_control_enabled() )
	{
		error = make_system_error_code(std::errc::operation_not_permitted);
		return 0;
	}
	return m_send_engine.write_control(op, payload, error);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::wait_written(error_code &error) noexcept
{
	m_send_engine.wait_written(error);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_wait_written(void_handler_t handler)
{
	m_send_engine.async_wait_written(std::move(handler));
}

template <core_concepts::exec Exec>
sys_expected<> basic_stream<Exec>::impl::queue_auto_pong(const std::vector<std::byte> &payload) noexcept
{
	return m_send_engine.queue_auto_pong(payload);
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_WRITE_IPP
