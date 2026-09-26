// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_READ_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_READ_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_H
# error "Include <riwo/websocket/detail/stream/receive_engine.h> instead."
#endif

namespace riwo::websocket::detail
{

template <typename Consumer>
message_info receive_engine::consume(Consumer &&consumer, error_code &error) noexcept
{
	error.clear();
	if( auto state_error = m_owner.consume_state_error() )
	{
		error = state_error;
		return {};
	}
	if( auto target_error = m_buffer.target_error(receive_target::chunk) )
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
		auto next = next_event(receive_target::chunk);
		if( next.error )
		{
			error = next.error;
			if( next.failure_origin == receive_failure_origin::protocol )
				m_owner.handle_receive_protocol_failure(error, true);
			else
				ignore_unused(finish_read_error(error));
			return {};
		}
		auto &value = *next.event;
		if( value.chunk )
		{
			try {
				std::invoke(consumer, *value.chunk);
			}
			catch(...)
			{
				error = exception_error(std::current_exception());
				m_owner.handle_receive_failure(error);
				return {};
			}
			if( value.chunk->last )
			{
				return message_info {
					.type = value.chunk->type,
					.size = value.chunk->offset + value.chunk->body.size(),
				};
			}
			continue;
		}
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

template <typename Consumer>
void receive_engine::async_consume(Consumer &&consumer, info_handler_t completion)
{
	auto self = m_owner.receive_owner();
	auto state_error = self->consume_state_error();

	if( not state_error )
		state_error = self->receive_side().m_buffer.target_error(receive_target::chunk);

	if( state_error or self->receive_side().m_read_active )
	{
		auto error = self->receive_side().m_read_active ?
			make_system_error_code(std::errc::operation_in_progress) : state_error;

		post_completion(self->receive_executor(),
			std::move(completion), error, message_info{}
		);
		return ;
	}
	std::shared_ptr<consume_wait_operation> waiter;
	try {
		auto associated_allocator = asio::get_associated_allocator(completion);

		using waiter_allocator_t = std::allocator_traits
			<decltype(associated_allocator)>::template rebind_alloc<consume_wait_operation>;

		waiter = std::allocate_shared
			<consume_wait_operation>(waiter_allocator_t(associated_allocator));

		waiter->id = ++self->receive_side().m_next_read_waiter_id;
		waiter->completion = std::move(completion);
		self->receive_side().m_consume_waiter = waiter;

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
			self->receive_side().complete_consume_waiter(error);
		else
		{
			post_completion(self->receive_executor(),
				std::move(completion), error, message_info{}
			);
		}
		return ;
	}
	self->receive_side().m_read_active = true;
	try
	{
		using consumer_t = std::remove_cvref_t<Consumer>;
		asio::co_spawn(self->receive_executor(),
		[self, consumer = consumer_t(std::forward<Consumer>(consumer))]
		() mutable -> awaitable<std::tuple<error_code, message_info>>
		{
			for(;;)
			{
				if( self->close_receive_pending() )
					co_return std::tuple<error_code,message_info>{make_error_code(errc::closing), {}};

				auto next = co_await self->receive_side().async_next_event (
					receive_target::chunk
				);
				if( next.error )
				{
					if( next.failure_origin == receive_failure_origin::protocol )
					{
						self->handle_receive_protocol_failure(next.error, false);
						co_return std::tuple<error_code,message_info>{next.error, {}};
					}
					if( next.failure_origin == receive_failure_origin::buffer )
					{
						self->handle_receive_failure(next.error);
						co_return std::tuple<error_code,message_info>{next.error, {}};
					}
					auto error = next.error;
					if( error == asio::error::eof )
						error = self->finish_receive_eof();

					else if( error != asio::error::operation_aborted )
						self->handle_receive_failure(error);

					co_return std::tuple<error_code,message_info>{error, {}};
				}
				auto &value = *next.event;
				if( value.chunk )
				{
					std::invoke(consumer, *value.chunk);
					if( value.chunk->last )
					{
						co_return std::tuple<error_code,message_info>{error_code{}, {
							.type = value.chunk->type,
							.size = value.chunk->offset + value.chunk->body.size(),
						}};
					}
					continue;
				}
				if( value.op == opcode::ping or value.op == opcode::pong )
				{
					auto control_error = co_await
						self->handle_async_control(value.op, value.control);

					if( control_error )
					{
						if( control_error != asio::error::operation_aborted )
							self->handle_receive_failure(control_error);

						co_return std::tuple<error_code,message_info>{control_error, {}};
					}
					continue;
				}
				if( value.op == opcode::close )
				{
					auto closing = self->begin_peer_close(value.control);
					if( not closing )
					{
						self->handle_receive_protocol_failure(closing.error(), false);
						co_return std::tuple<error_code,message_info>{closing.error(), {}};
					}
					co_return std::tuple<error_code,message_info>{asio::error::eof, {}};
				}
			}
		},
		asio::bind_executor(self->receive_executor(),
		asio::bind_cancellation_slot(waiter->cancellation.slot(), [self, waiter]
		(const std::exception_ptr &exception, std::tuple<error_code, message_info> result) mutable
		{
			ignore_unused(waiter);
			self->receive_side().m_read_active = false;

			if( auto error = exception_error(exception) )
			{
				if( error != asio::error::operation_aborted )
					self->handle_receive_failure(error);
				self->receive_side().complete_consume_waiter(error);
			}
			else
			{
				auto [result_error, value] = std::move(result);
				self->receive_side().complete_consume_waiter(result_error, value);
			}
			if( self->close_receive_pending() )
				self->start_close_receive(); })
		));
	}
	catch(...)
	{
		self->receive_side().m_read_active = false;
		self->receive_side().complete_consume_waiter (
			exception_error(std::current_exception())
		);
	}
}

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_ENGINE_READ_IPP
