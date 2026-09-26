// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CORE_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CORE_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
# error "Include <riwo/websocket/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket
{

template <core_concepts::exec Exec>
basic_stream<Exec>::impl::impl(executor_t exec, const config_t &config) :
	m_exec(std::move(exec)), m_config(config),
	m_receive_engine(*this), m_send_engine(*this)
{

}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::receive_side() noexcept -> detail::receive_engine&
{
	return m_receive_engine;
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::receive_owner() noexcept -> std::shared_ptr<receive_engine_owner>
{
	return this->shared_from_this();
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::weak_receive_owner() noexcept -> std::weak_ptr<receive_engine_owner>
{
	return this->weak_from_this();
}

template <core_concepts::exec Exec>
asio::any_io_executor basic_stream<Exec>::impl::receive_executor() const noexcept
{
	return m_exec;
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::send_side() noexcept -> detail::send_engine&
{
	return m_send_engine;
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::send_owner() noexcept -> std::shared_ptr<send_engine_owner>
{
	return this->shared_from_this();
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::weak_send_owner() noexcept -> std::weak_ptr<send_engine_owner>
{
	return this->weak_from_this();
}

template <core_concepts::exec Exec>
asio::any_io_executor basic_stream<Exec>::impl::send_executor() const noexcept
{
	return m_exec;
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::impl::executor() const noexcept -> executor_t
{
	return m_exec;
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::write_state_error() const noexcept
{
	if( m_state == connection_state::open )
		return {};

	if( m_state == connection_state::failed )
		return m_error ? m_error : make_system_error_code(std::errc::io_error);

	if( m_state == connection_state::closed )
		return make_error_code(errc::closed);

	if( m_state == connection_state::closing )
	{
		return m_peer_close ?
			make_system_error_code(std::errc::broken_pipe) :
			make_error_code(errc::closing);
	}
	return make_error_code(errc::not_open);
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::read_state_error() const noexcept
{
	if( m_state == connection_state::open )
		return {};

	if( m_state == connection_state::closing )
		return make_error_code(errc::closing);

	if( m_state == connection_state::closed )
		return asio::error::eof;

	if( m_state == connection_state::failed )
		return m_error ? m_error : make_system_error_code(std::errc::io_error);

	return make_error_code(errc::not_open);
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::frame_read_state_error() const noexcept
{
	return read_state_error();
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::frame_write_state_error() const noexcept
{
	return write_state_error();
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::consume_state_error() const noexcept
{
	return read_state_error();
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::impl::send_transport_ready() const noexcept
{
	return m_connection and not m_transport_closed and
		   m_state != connection_state::closed and
		   (m_state != connection_state::failed or m_protocol_failure_active);
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::impl::automatic_control_enabled() const noexcept
{
	return m_config.ping_interval > std::chrono::milliseconds::zero();
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::impl::close_receive_pending() const noexcept
{
	return local_close_started() and m_state == connection_state::closing;
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::impl::protocol_failure_active() const noexcept
{
	return m_protocol_failure_active;
}

template <core_concepts::exec Exec>
error_code basic_stream<Exec>::impl::protocol_failure_error(error_code fallback) const noexcept
{
	return m_protocol_failure_active and m_error ? m_error : fallback;
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::adopt
(connection_ptr connection, adopt_options_t options, error_code &error) noexcept
{
	if( m_state != connection_state::idle )
	{
		error = make_error_code(errc::already_open);
		return ;
	}
	if( not connection )
	{
		error = make_system_error_code(std::errc::invalid_argument);
		return ;
	}
	if( m_config.read_buffer_size == 0 or
		m_config.ping_interval < std::chrono::milliseconds::zero() or
		m_config.compression.level < -1 or m_config.compression.level > 9 )
	{
		error = make_system_error_code(std::errc::invalid_argument);
		return ;
	}
	if( options.stream_role != role::client and options.stream_role != role::server )
	{
		error = make_system_error_code(std::errc::invalid_argument);
		return ;
	}
	if( not detail::supported_negotiated_extensions(options.negotiated_extensions) )
	{
		error = make_error_code(errc::unsupported_extension);
		return ;
	}
	if( not connection->is_open() )
	{
		error = make_system_error_code(std::errc::not_connected);
		return ;
	}
	try {
		auto connection_exec = connection->get_executor();
		if constexpr( requires { connection_exec != m_exec; } )
		{
			if( connection_exec != m_exec )
			{
				error = make_system_error_code(std::errc::invalid_argument);
				return ;
			}
		}
		m_role = options.stream_role;
		m_send_engine.reset(m_role, m_config, options.negotiated_extensions);

		m_receive_engine.reset(m_role, m_config,
			options.negotiated_extensions, std::move(options.pending_data)
		);
		m_subprotocol = std::move(options.negotiated_subprotocol);
		m_extensions = std::move(options.negotiated_extensions);
		m_connection = std::move(connection);

		m_stream_transport.reset(m_connection,
			&impl::start_async_transport_read,
			&impl::start_async_transport_write
		);
		m_state = connection_state::open;
		if( auto started = start_automatic_ping(); not started )
		{
			error = started.error();
			fail(error);
			return ;
		}
		error.clear();
	}
	catch(const std::bad_alloc&) {
		error = make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {
		error = make_system_error_code(std::errc::io_error);
	}
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CORE_IPP
