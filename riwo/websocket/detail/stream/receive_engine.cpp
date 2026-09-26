// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#define RIWO_WEBSOCKET_RECEIVE_ENGINE_IMPLEMENTATION
#include <riwo/websocket/detail/stream/receive_engine.h>
#include <riwo/websocket/detail/stream/receive_engine_read.ipp>

namespace riwo::websocket::detail
{

receive_engine::receive_engine(receive_engine_owner &owner) noexcept :
	m_owner(owner)
{

}

receive_engine::~receive_engine() = default;

void receive_engine::reset(role local_role, const stream_config &config,
	std::span<const extension> extensions, std::vector<std::byte> pending_data)
{
	m_buffer.reset(local_role, config, extensions, std::move(pending_data));
	m_read_error.clear();
	m_read_active = false;
}

bool receive_engine::active() const noexcept
{
	return m_read_active;
}

void receive_engine::set_active(bool value) noexcept
{
	m_read_active = value;
}

receive_event_result receive_engine::next_event(receive_target target) noexcept
{
	for(;;)
	{
		while( m_buffer.available_data().size() != 0 )
		{
			auto event = m_buffer.consume(target);
			if( not event )
			{
				return {
					.error = event.error(),
					.failure_origin = receive_failure_origin::protocol,
				};
			}
			if( *event )
				return {.event = std::move(**event)};
		}
		if( m_read_error )
		{
			return {
				.error = std::exchange(m_read_error, {}),
				.failure_origin = receive_failure_origin::transport,
			};
		}
		error_code read_error;
		auto storage = m_buffer.read_storage();

		const auto size = m_owner.read_transport (
			mutable_buffer(storage->data(), storage->size()), read_error
		);
		if( auto error = m_buffer.commit_read(size) )
		{
			return {
				.error = error,
				.failure_origin = receive_failure_origin::buffer,
			};
		}
		m_read_error = read_error;
		if( size == 0 )
		{
			auto error = std::exchange(m_read_error, {});
			if( not error )
				error = make_system_error_code(std::errc::io_error);

			return {
				.error = error,
				.failure_origin = receive_failure_origin::transport,
			};
		}
	}
}

awaitable<receive_event_result> receive_engine::async_next_event(receive_target target)
{
	for(;;)
	{
		while( m_buffer.available_data().size() != 0 )
		{
			auto event = m_buffer.consume(target);
			if( not event )
			{
				co_return receive_event_result {
					.error = event.error(),
					.failure_origin = receive_failure_origin::protocol,
				};
			}
			if( *event )
				co_return receive_event_result {.event = std::move(**event)};
		}
		if( m_read_error )
		{
			co_return receive_event_result {
				.error = std::exchange(m_read_error, {}),
				.failure_origin = receive_failure_origin::transport,
			};
		}
		auto storage = m_buffer.read_storage();
		auto [read_error, size] = co_await m_owner.async_read_transport(storage);

		if( auto error = m_buffer.commit_read(size) )
		{
			co_return receive_event_result {
				.error = error,
				.failure_origin = receive_failure_origin::buffer,
			};
		}
		m_read_error = read_error;
		if( size == 0 )
		{
			auto error = std::exchange(m_read_error, {});
			if( not error )
				error = make_system_error_code(std::errc::io_error);

			co_return receive_event_result {
				.error = error,
				.failure_origin = receive_failure_origin::transport,
			};
		}
	}
}

message receive_engine::read(error_code &error) noexcept
{
	error.clear();
	if( auto state_error = m_owner.read_state_error() )
	{
		error = state_error;
		return {};
	}
	if( auto target_error = m_buffer.target_error(receive_target::message) )
	{
		error = target_error;
		return {};
	}
	if( m_read_active )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return {};
	}
	m_read_active = true;

	struct read_guard
	{
		bool &active;
		~read_guard() {
			active = false;
		}
	}
	guard{m_read_active};

	for(;;)
	{
		auto next = next_event(receive_target::message);
		if( next.error )
		{
			error = next.error;
			if( next.failure_origin == receive_failure_origin::protocol )
			{
				m_owner.handle_receive_protocol_failure(error, true);
				return {};
			}
			if( next.failure_origin == receive_failure_origin::buffer )
			{
				m_owner.handle_receive_failure(error);
				return {};
			}
			return finish_read_error(error);
		}
		auto &value = *next.event;
		if( value.data )
			return std::move(*value.data);

		if( value.op == opcode::ping or value.op == opcode::pong )
		{
			if( auto handled = m_owner.handle_sync_control(value.op, value.control); not handled )
			{
				error = handled.error();
				m_owner.handle_receive_failure(error);
				return {};
			}
			continue;
		}
		if( value.op == opcode::close )
		{
			if( auto closed = m_owner.handle_sync_peer_close(value.control); not closed )
			{
				error = closed.error();
				m_owner.handle_receive_protocol_failure(error, true);
				return {};
			}
			error = asio::error::eof;
			return {};
		}
	}
}

data_frame receive_engine::read_frame(error_code &error) noexcept
{
	error.clear();
	if( auto state_error = m_owner.frame_read_state_error() )
	{
		error = state_error;
		return {};
	}
	if( auto target_error = m_buffer.target_error(receive_target::frame) )
	{
		error = target_error;
		return {};
	}
	if( m_read_active )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return {};
	}
	m_read_active = true;

	struct read_guard
	{
		bool &active;
		~read_guard() {
			active = false;
		}
	}
	guard{m_read_active};

	for(;;)
	{
		auto next = next_event(receive_target::frame);
		if( next.error )
		{
			error = next.error;
			if( next.failure_origin == receive_failure_origin::protocol )
			{
				m_owner.handle_receive_protocol_failure(error, true);
				return {};
			}
			if( next.failure_origin == receive_failure_origin::buffer )
			{
				m_owner.handle_receive_failure(error);
				return {};
			}
			ignore_unused(finish_read_error(error));
			return {};
		}
		auto &value = *next.event;
		if( value.frame )
			return std::move(*value.frame);

		if( value.op == opcode::ping or value.op == opcode::pong )
		{
			if( auto handled = m_owner.handle_sync_control(value.op, value.control); not handled )
			{
				error = handled.error();
				m_owner.handle_receive_failure(error);
				return {};
			}
			continue;
		}
		if( value.op == opcode::close )
		{
			if( auto closed = m_owner.handle_sync_peer_close(value.control); not closed )
			{
				error = closed.error();
				m_owner.handle_receive_protocol_failure(error, true);
				return {};
			}
			error = asio::error::eof;
			return {};
		}
	}
}

void receive_engine::async_read_message(message_handler_t completion)
{
	auto self = m_owner.receive_owner();
	auto state_error = self->read_state_error();

	if( not state_error )
		state_error = self->receive_side().m_buffer.target_error(receive_target::message);

	if( state_error or self->receive_side().m_read_active )
	{
		auto error = self->receive_side().m_read_active ?
			make_system_error_code(std::errc::operation_in_progress) : state_error;

		post_completion(self->receive_executor(),
			std::move(completion), error, message{}
		);
		return ;
	}
	std::shared_ptr<read_wait_operation> waiter;
	try {
		auto associated_allocator = asio::get_associated_allocator(completion);

		using waiter_allocator_t = std::allocator_traits
			<decltype(associated_allocator)>::template rebind_alloc<read_wait_operation>;

		waiter = std::allocate_shared
			<read_wait_operation>(waiter_allocator_t(associated_allocator));

		waiter->id = ++self->receive_side().m_next_read_waiter_id;
		waiter->completion = std::move(completion);
		self->receive_side().m_read_waiter = waiter;

		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion); slot.is_connected() )
		{
			slot.assign([weak = self->weak_receive_owner(), id = waiter->id]
			(asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto locked = weak.lock() )
				{
					try {
						asio::dispatch(locked->receive_executor(), [locked, id, type]{
							locked->receive_side().cancel_read_waiter(id, type);
						});
					}
					catch(...) {}
				}
			});
		}
	}
	catch(...)
	{
		auto error = exception_error(std::current_exception());
		if( waiter )
			self->receive_side().complete_read_waiter(error);
		else
		{
			riwo::post_completion(self->receive_executor(),
				std::move(completion), error, message{}
			);
		}
		return ;
	}
	self->receive_side().m_read_active = true;
	try
	{
		asio::co_spawn(self->receive_executor(),
		[self]() -> awaitable<std::tuple<error_code, message>>
		{
			for(;;)
			{
				if( self->close_receive_pending() )
					co_return std::tuple<error_code,message>{make_error_code(errc::closing), {}};

				auto next = co_await self->receive_side().async_next_event (
					receive_target::message
				);
				if( next.error )
				{
					if( next.failure_origin == receive_failure_origin::protocol )
					{
						self->handle_receive_protocol_failure(next.error, false);
						co_return std::tuple<error_code,message>{next.error, {}};
					}
					if( next.failure_origin == receive_failure_origin::buffer )
					{
						self->handle_receive_failure(next.error);
						co_return std::tuple<error_code,message>{next.error, {}};
					}
					auto error = next.error;
					if( error == asio::error::eof )
						error = self->finish_receive_eof();

					else if( error != asio::error::operation_aborted )
						self->handle_receive_failure(error);

					co_return std::tuple<error_code,message>{error, {}};
				}
				auto &value = *next.event;
				if( value.data )
					co_return std::tuple{error_code{}, std::move(*value.data)};

				if( value.op == opcode::ping or value.op == opcode::pong )
				{
					auto control_error = co_await
						self->handle_async_control(value.op, value.control);

					if( control_error )
					{
						if( control_error != asio::error::operation_aborted )
							self->handle_receive_failure(control_error);

						co_return std::tuple<error_code,message>{control_error, {}};
					}
					continue;
				}
				if( value.op == opcode::close )
				{
					if( auto closing = self->begin_peer_close(value.control); not closing )
					{
						self->handle_receive_protocol_failure(closing.error(), false);
						co_return std::tuple<error_code,message>{closing.error(), {}};
					}
					co_return std::tuple<error_code,message>{asio::error::eof, {}};
				}
			}
		},
		asio::bind_executor(self->receive_executor(),
		asio::bind_cancellation_slot(waiter->cancellation.slot(), [self, waiter]
		(const std::exception_ptr &exception, std::tuple<error_code, message> result) mutable
		{
			ignore_unused(waiter);
			self->receive_side().m_read_active = false;

			if( auto error = exception_error(exception) )
			{
				if( error != asio::error::operation_aborted )
					self->handle_receive_failure(error);
				self->receive_side().complete_read_waiter(error);
			}
			else
			{
				auto [result_error, value] = std::move(result);
				self->receive_side().complete_read_waiter(result_error, std::move(value));
			}
			if( self->close_receive_pending() )
				self->start_close_receive(); })
		));
	}
	catch(...)
	{
		self->receive_side().m_read_active = false;
		self->receive_side().complete_read_waiter(exception_error(std::current_exception()));
	}
}

void receive_engine::async_read_frame(frame_handler_t completion)
{
	auto self = m_owner.receive_owner();
	auto state_error = self->frame_read_state_error();

	if( not state_error )
		state_error = self->receive_side().m_buffer.target_error(receive_target::frame);

	if( state_error or self->receive_side().m_read_active )
	{
		auto error = self->receive_side().m_read_active ?
			make_system_error_code(std::errc::operation_in_progress) : state_error;

		post_completion(self->receive_executor(),
			std::move(completion), error, data_frame{}
		);
		return ;
	}
	std::shared_ptr<frame_read_wait_operation> waiter;
	try {
		auto associated_allocator = asio::get_associated_allocator(completion);

		using waiter_allocator_t = std::allocator_traits
			<decltype(associated_allocator)>::template rebind_alloc<frame_read_wait_operation>;

		waiter = std::allocate_shared
			<frame_read_wait_operation>(waiter_allocator_t(associated_allocator));

		waiter->id = ++self->receive_side().m_next_read_waiter_id;
		waiter->completion = std::move(completion);
		self->receive_side().m_frame_read_waiter = waiter;

		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion); slot.is_connected() )
		{
			slot.assign([weak = self->weak_receive_owner(), id = waiter->id]
			(asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto locked = weak.lock() )
				{
					try {
						asio::dispatch(locked->receive_executor(), [locked, id, type]{
							locked->receive_side().cancel_read_waiter(id, type);
						});
					}
					catch(...) {}
				}
			});
		}
	}
	catch(...)
	{
		auto error = exception_error(std::current_exception());
		if( waiter )
			self->receive_side().complete_frame_read_waiter(error);
		else
		{
			post_completion(self->receive_executor(),
				std::move(completion), error, data_frame{}
			);
		}
		return ;
	}
	self->receive_side().m_read_active = true;
	try
	{
		asio::co_spawn(self->receive_executor(),
		[self]() -> awaitable<std::tuple<error_code,data_frame>>
		{
			for(;;)
			{
				if( self->close_receive_pending() )
					co_return std::tuple<error_code,data_frame>{make_error_code(errc::closing), {}};

				auto next = co_await self->receive_side().async_next_event (
					receive_target::frame
				);
				if( next.error )
				{
					if( next.failure_origin == receive_failure_origin::protocol )
					{
						self->handle_receive_protocol_failure(next.error, false);
						co_return std::tuple<error_code,data_frame>{next.error, {}};
					}
					if( next.failure_origin == receive_failure_origin::buffer )
					{
						self->handle_receive_failure(next.error);
						co_return std::tuple<error_code,data_frame>{next.error, {}};
					}
					auto error = next.error;
					if( error == asio::error::eof )
						error = self->finish_receive_eof();

					else if( error != asio::error::operation_aborted )
						self->handle_receive_failure(error);

					co_return std::tuple<error_code,data_frame>{error, {}};
				}
				auto &value = *next.event;
				if( value.frame )
					co_return std::tuple{error_code{}, std::move(*value.frame)};

				if( value.op == opcode::ping or value.op == opcode::pong )
				{
					auto control_error = co_await
						self->handle_async_control(value.op, value.control);

					if( control_error )
					{
						if( control_error != asio::error::operation_aborted )
							self->handle_receive_failure(control_error);

						co_return std::tuple<error_code,data_frame>{control_error, {}};
					}
					continue;
				}
				if( value.op == opcode::close )
				{
					if( auto closing = self->begin_peer_close(value.control); not closing )
					{
						self->handle_receive_protocol_failure(closing.error(), false);
						co_return std::tuple<error_code,data_frame>{closing.error(), {}};
					}
					co_return std::tuple<error_code,data_frame>{asio::error::eof, {}};
				}
			}
		},
		asio::bind_executor(self->receive_executor(),
		asio::bind_cancellation_slot(waiter->cancellation.slot(), [self, waiter]
		(const std::exception_ptr &exception, std::tuple<error_code, data_frame> result) mutable
		{
			ignore_unused(waiter);
			self->receive_side().m_read_active = false;

			if( auto error = exception_error(exception) )
			{
				if( error != asio::error::operation_aborted )
					self->handle_receive_failure(error);
				self->receive_side().complete_frame_read_waiter(error);
			}
			else
			{
				auto [result_error, value] = std::move(result);
				self->receive_side().complete_frame_read_waiter (
					result_error, std::move(value)
				);
			}
			if( self->close_receive_pending() )
				self->start_close_receive(); })
		));
	}
	catch(...)
	{
		self->receive_side().m_read_active = false;
		self->receive_side().complete_frame_read_waiter (
			exception_error(std::current_exception())
		);
	}
}

void receive_engine::start_close_receive() noexcept
{
	if( m_read_active )
		return ;

	m_read_active = true;
	auto self = m_owner.receive_owner();
	try {
		auto operation = [](std::shared_ptr<receive_engine_owner> owner) -> awaitable<error_code>
		{
			for(;;)
			{
				if( not owner->close_receive_pending() )
					co_return asio::error::operation_aborted;

				auto next = co_await owner->receive_side().async_next_event();
				if( next.error )
					co_return next.error;

				auto &value = *next.event;
				if( value.data )
					continue;

				if( value.op == opcode::ping or value.op == opcode::pong )
				{
					auto control_error = co_await
						owner->handle_async_control(value.op, value.control);

					if( control_error )
						co_return control_error;
					continue;
				}
				if( value.op == opcode::close )
				{
					auto remembered = owner->remember_close_receive_peer(value.control);
					co_return remembered ? error_code{} : remembered.error();
				}
			}
		}
		(self);

		auto exec = self->receive_executor();
		riwo::detail::launch_awaitable(exec, std::move(operation),
			close_receive_handler{std::move(self)}
		);
	}
	catch(...)
	{
		m_read_active = false;
		m_owner.complete_close_receive(std::current_exception(), {});
	}
}

void receive_engine::complete_read_waiter(error_code error, message value, bool clear_slot) noexcept
{
	if( not m_read_waiter )
	{
		if( error and m_frame_read_waiter )
			complete_frame_read_waiter(error, {}, clear_slot);

		else if( error )
			complete_consume_waiter(error, {}, clear_slot);
		return ;
	}
	auto waiter = std::exchange(m_read_waiter, {});
	if( clear_slot )
	{
		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
			slot.clear();
	}
	try {
		auto completion = std::move(waiter->completion);
		std::move(completion)(error, std::move(value));
	}
	catch(...) {}
}

void receive_engine::complete_frame_read_waiter
(error_code error, data_frame value, bool clear_slot) noexcept
{
	if( not m_frame_read_waiter )
		return ;

	auto waiter = std::exchange(m_frame_read_waiter, {});
	if( clear_slot )
	{
		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
			slot.clear();
	}
	try {
		auto completion = std::move(waiter->completion);
		std::move(completion)(error, std::move(value));
	}
	catch(...) {}
}

void receive_engine::complete_consume_waiter
(error_code error, message_info value, bool clear_slot) noexcept
{
	if( not m_consume_waiter )
		return ;

	auto waiter = std::exchange(m_consume_waiter, {});
	if( clear_slot )
	{
		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
			slot.clear();
	}
	try {
		auto completion = std::move(waiter->completion);
		std::move(completion)(error, value);
	}
	catch(...) {}
}

void receive_engine::close_receive_handler::operator()
(const std::exception_ptr &exception, error_code error) const noexcept
{
	owner->receive_side().m_read_active = false;
	owner->complete_close_receive(exception, error);
}

message receive_engine::finish_read_error(error_code &error) noexcept
{
	if( error == asio::error::operation_aborted )
		return {};

	if( error == asio::error::eof )
	{
		error = m_owner.finish_receive_eof();
		return {};
	}
	m_owner.handle_receive_failure(error);
	return {};
}

void receive_engine::cancel_read_waiter(uint64_t id, asio::cancellation_type type) noexcept
{
	try {
		if( m_read_waiter and m_read_waiter->id == id )
			m_read_waiter->cancellation.emit(type);

		else if( m_frame_read_waiter and m_frame_read_waiter->id == id )
			m_frame_read_waiter->cancellation.emit(type);

		else if( m_consume_waiter and m_consume_waiter->id == id )
			m_consume_waiter->cancellation.emit(type);
	}
	catch(...) {}
}

} //namespace riwo::websocket::detail
