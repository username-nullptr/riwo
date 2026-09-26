// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CONTROL_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CONTROL_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
# error "Include <riwo/websocket/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket
{

template <core_concepts::exec Exec>
sys_expected<> basic_stream<Exec>::impl::handle_sync_ping(ctrl_payload_t &payload) noexcept
{
	if( not automatic_control_enabled() )
		return make_sys_expected();

	if( m_send_engine.busy() )
		return queue_auto_pong(payload.storage());

	auto frame = m_send_engine.prepare_control(opcode::pong,
		payload.as_const_buffer()
	);
	if( not frame )
		return sys_unexpected(frame.error());

	error_code error;
	ignore_unused(write_prepared(*frame, error));
	return error ? sys_expected<>(sys_unexpected(error)) : make_sys_expected();
}

template <core_concepts::exec Exec>
sys_expected<> basic_stream<Exec>::impl::handle_sync_control
(opcode op, std::vector<std::byte> &payload) noexcept
{
	if( op == opcode::pong )
		acknowledge_automatic_pong(payload);

	auto &callback = op == opcode::ping ? m_on_ping : m_on_pong;

	if( auto &async_callback = op == opcode::ping ?
		m_on_async_ping : m_on_async_pong; async_callback )
	{
		return sys_unexpected (
			make_system_error_code(std::errc::operation_not_supported)
		);
	}
	try {
		ctrl_payload_t callback_payload(std::move(payload));
		if( callback )
			callback(callback_payload);

		return op == opcode::ping ?
			handle_sync_ping(callback_payload) : make_sys_expected();
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

template <core_concepts::exec Exec>
awaitable<error_code> basic_stream<Exec>::impl::handle_async_control
(opcode op, std::vector<std::byte> &payload)
{
	if( op == opcode::pong )
		acknowledge_automatic_pong(payload);

	auto &callback = op == opcode::ping ? m_on_ping : m_on_pong;
	auto &async_callback = op == opcode::ping ? m_on_async_ping : m_on_async_pong;
	try {
		ctrl_payload_t callback_payload(std::move(payload));
		if( async_callback )
			co_await async_callback(callback_payload);

		else if( callback )
			callback(callback_payload);

		if( op == opcode::ping and automatic_control_enabled() )
		{
			auto queued = queue_auto_pong(callback_payload.storage());
			co_return queued ? error_code{} : queued.error();
		}
	}
	catch(...) {
		co_return exception_error(std::current_exception());
	}
	co_return error_code{};
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::on_ping(sync_control_callback_t callback)
{
	m_on_ping = std::move(callback);
	m_on_async_ping = {};
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::on_ping(async_control_callback_t callback)
{
	m_on_ping = {};
	m_on_async_ping = std::move(callback);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::on_pong(sync_control_callback_t callback)
{
	m_on_pong = std::move(callback);
	m_on_async_pong = {};
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::on_pong(async_control_callback_t callback)
{
	m_on_pong = {};
	m_on_async_pong = std::move(callback);
}

template <core_concepts::exec Exec>
sys_expected<> basic_stream<Exec>::impl::start_automatic_ping() noexcept
{
	if( not automatic_control_enabled() )
		return make_sys_expected();

	auto ping = detail::automatic_ping::create(asio::any_io_executor(m_exec),
		std::weak_ptr<void>(this->shared_from_this()), m_config.ping_interval,
		m_config.pong_timeout_retries, &impl::write_automatic_ping,
		&impl::fail_automatic_ping
	);
	if( not ping )
		return sys_unexpected(ping.error());

	m_automatic_ping = std::move(*ping);
	return make_sys_expected();
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::acknowledge_automatic_pong(const std::vector<std::byte> &payload) noexcept
{
	if( m_automatic_ping )
		m_automatic_ping->acknowledge(payload);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::stop_automatic_ping() noexcept
{
	if( auto ping = std::exchange(m_automatic_ping, {}) )
		ping->stop();
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::write_automatic_ping
(void *owner, const const_buffer &payload, io_handler_t handler)
{
	static_cast<impl*>(owner)->async_write_control (
		opcode::ping, payload, std::move(handler), true
	);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::fail_automatic_ping(void *owner, error_code error) noexcept
{
	static_cast<impl*>(owner)->fail(error);
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_CONTROL_IPP
