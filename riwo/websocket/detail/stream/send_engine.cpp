// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/send_engine.h>

namespace riwo::websocket::detail
{

send_engine::send_engine(send_engine_owner &owner) noexcept :
	m_owner(owner)
{

}

send_engine::~send_engine() = default;

void send_engine::reset
(role local_role, const stream_config &config, std::span<const extension> extensions) noexcept
{
	m_frame_builder.reset(local_role, config, extensions);

	m_max_queued_write_bytes = config.max_queued_write_bytes;
	m_max_queued_write_operations = config.max_queued_write_operations;
	m_max_message_size = config.max_message_size;

	m_outgoing_message_type.reset();
	m_outgoing_message_size = 0;
	m_outgoing_utf8.reset();
}

auto send_engine::prepare_control(opcode op, const const_buffer &payload, bool borrow_payload)
	const noexcept -> sys_expected<prepared_frame>
{
	return m_frame_builder.prepare_control(op, payload, borrow_payload);
}

auto send_engine::prepare_close(const close_frame &frame)
	const noexcept -> sys_expected<prepared_frame>
{
	return m_frame_builder.prepare_close(frame);
}

auto send_engine::prepare_message
(message_type type, std::span<const const_buffer> buffers, write_options options)
	const noexcept -> sys_expected<std::vector<prepared_frame>>
{
	if( m_outgoing_message_type )
		return sys_unexpected(make_error_code(protocol_errc::data_during_fragmentation));

	if( (m_current_data and m_current_data->explicit_data_frame) or
		std::ranges::any_of(m_data_write_queue, [](const auto &operation) {
			return operation->explicit_data_frame;
		}) )
		return sys_unexpected(make_system_error_code(std::errc::operation_in_progress));

	return m_frame_builder.prepare_message(type, buffers, options);
}

auto send_engine::prepare_data_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin)
	const noexcept -> sys_expected<prepared_data_frame>
{
	try {
		if( type != message_type::text and type != message_type::binary )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( payload.size() != 0 and payload.data() == nullptr )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( m_outgoing_message_type )
		{
			if( not continuation )
				return sys_unexpected(make_error_code(protocol_errc::data_during_fragmentation));

			if( type != *m_outgoing_message_type )
				return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
		}
		else if( continuation )
			return sys_unexpected(make_error_code(protocol_errc::unexpected_continuation));

		if( payload.size() > std::numeric_limits<size_t>::max() - m_outgoing_message_size )
			return sys_unexpected(make_system_error_code(std::errc::value_too_large));

		const auto message_size = m_outgoing_message_size + payload.size();
		if( m_max_message_size != 0 and message_size > m_max_message_size )
			return sys_unexpected(make_error_code(errc::message_too_big));

		optional<utf8_validator> next_utf8;
		if( type == message_type::text )
		{
			next_utf8 = m_outgoing_utf8 ? *m_outgoing_utf8 : utf8_validator {};
			const auto *data = static_cast<const char*>(payload.data());

			const auto text = payload.size() == 0 ?
				std::string_view{} : std::string_view(data, payload.size());

			if( not next_utf8->consume(text) or (fin and not next_utf8->complete()) )
				return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
		}
		auto frame = m_frame_builder.prepare_data_frame (
			type, payload, continuation, fin, true
		);
		if( not frame )
			return sys_unexpected(frame.error());

		prepared_data_frame result {.frame = std::move(*frame)};
		if( not fin )
		{
			result.next_message_type = type;
			result.next_message_size = message_size;
			result.next_utf8 = std::move(next_utf8);
		}
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

bool send_engine::busy() const noexcept
{
	return m_wire_write_active or m_current_data or
		   not m_data_write_queue.empty() or not m_control_write_queue.empty() or
		   m_pending_auto_pong or m_pending_local_close or
		   m_pending_close_response or m_pending_protocol_close;
}

bool send_engine::wire_write_active() const noexcept
{
	return m_wire_write_active;
}

bool send_engine::current_data_active() const noexcept
{
	return static_cast<bool>(m_current_data);
}

bool send_engine::data_busy() const noexcept
{
	return m_current_data or not m_data_write_queue.empty();
}

bool send_engine::ready_for_sync_protocol_write() const noexcept
{
	return not m_wire_write_active and not m_current_data;
}

void send_engine::fail_current_if_idle(error_code error) noexcept
{
	if( m_wire_write_active or not m_current_data )
		return ;

	auto operation = std::exchange(m_current_data, {});
	complete_send_operation(operation, error);
}

void send_engine::fail_queued_controls(error_code error)
{
	while(not m_control_write_queue.empty())
	{
		auto operation = std::move(m_control_write_queue.front());
		m_control_write_queue.pop_front();

		if( operation->queued_counted )
			m_queued_write_operations--;

		complete_send_operation(operation, error);
	}
}

void send_engine::fail_queued_writes(error_code error)
{
	fail_queued_controls(error);
	while(not m_data_write_queue.empty())
	{
		auto operation = std::move(m_data_write_queue.front());
		m_data_write_queue.pop_front();

		if( operation->queued_counted )
		{
			m_queued_write_operations--;
			m_queued_write_bytes -= operation->queued_payload_size;
		}
		complete_send_operation(operation, operation->cancel_requested ?
			error_code(asio::error::operation_aborted) : error
		);
	}
}

void send_engine::clear_auto_pong() noexcept
{
	m_pending_auto_pong.reset();
}

void send_engine::clear_local_close() noexcept
{
	m_pending_local_close.reset();
}

void send_engine::clear_close_response() noexcept
{
	m_pending_close_response.reset();
}

void send_engine::clear_protocol_close() noexcept
{
	m_pending_protocol_close.reset();
}

void send_engine::clear_protocol_frames() noexcept
{
	clear_auto_pong();
	clear_local_close();
	clear_close_response();
	clear_protocol_close();
}

void send_engine::queue_local_close(prepared_frame frame)
{
	m_pending_local_close = std::move(frame);
}

void send_engine::queue_protocol_close(prepared_frame frame)
{
	m_pending_protocol_close = std::move(frame);
}

bool send_engine::has_local_close() const noexcept
{
	return m_pending_local_close.has_value();
}

sys_expected<> send_engine::queue_close_response(const std::vector<std::byte> &payload) noexcept
{
	return retain_protocol_payload(m_pending_close_response, payload);
}

sys_expected<> send_engine::queue_auto_pong(const std::vector<std::byte> &payload) noexcept
{
	auto retained = retain_protocol_payload(m_pending_auto_pong, payload);
	if( retained )
		schedule();
	return retained;
}

void detail::send_engine::schedule()
{
	if( m_wire_write_active or not m_owner.send_transport_ready() )
		return ;

	if( m_pending_protocol_close and not m_current_data )
	{
		auto frame = std::exchange(m_pending_protocol_close, nullopt).value();
		start_wire_frame(std::move(frame), wire_frame_kind::protocol_close);
		return ;
	}
	if( m_pending_close_response and not m_current_data )
	{
		auto payload = std::exchange(m_pending_close_response, nullopt);
		m_pending_auto_pong.reset();

		auto frame = m_frame_builder.prepare_control(opcode::close,
			const_buffer(payload->data(), payload->size())
		);
		if( not frame )
		{
			fail_queued_writes(frame.error());
			m_owner.handle_send_failure(frame.error());
			return ;
		}
		start_wire_frame(std::move(*frame), wire_frame_kind::close_response);
		return ;
	}
	if( m_pending_local_close and not m_current_data and m_data_write_queue.empty() )
	{
		auto frame = std::exchange(m_pending_local_close, nullopt).value();
		m_pending_auto_pong.reset();

		fail_queued_controls(make_error_code(errc::closing));
		start_wire_frame(std::move(frame), wire_frame_kind::local_close);
		return ;
	}
	const bool data_available = m_current_data or not m_data_write_queue.empty();

	if( (not m_last_wire_was_control or not data_available) and
		m_pending_auto_pong and not m_pending_local_close and
		not m_pending_close_response )
	{
		auto payload = std::exchange(m_pending_auto_pong, nullopt);
		auto frame = m_frame_builder.prepare_control(opcode::pong,
			const_buffer(payload->data(), payload->size())
		);
		if( not frame )
		{
			fail_queued_writes(frame.error());
			m_owner.handle_send_failure(frame.error());
			return ;
		}
		start_wire_frame(std::move(*frame), wire_frame_kind::auto_pong);
		return ;
	}
	if( (not m_last_wire_was_control or not data_available) and
		not m_control_write_queue.empty() and not m_pending_local_close and
		not m_pending_close_response )
	{
		auto operation = std::move(m_control_write_queue.front());
		m_control_write_queue.pop_front();

		if( operation->queued_counted )
		{
			m_queued_write_operations--;
			operation->queued_counted = false;
		}
		auto frame = std::move(operation->frames.front());

		start_wire_frame(std::move(frame),
			wire_frame_kind::application_control, std::move(operation)
		);
		return ;
	}
	if( data_available )
	{
		if( not m_current_data )
		{
			while( not m_data_write_queue.empty() and m_data_write_queue.front()->cancel_requested )
			{
				auto cancelled = std::move(m_data_write_queue.front());
				m_data_write_queue.pop_front();

				if( cancelled->queued_counted )
				{
					m_queued_write_operations--;
					m_queued_write_bytes -= cancelled->queued_payload_size;
					cancelled->queued_counted = false;
				}
				complete_send_operation(cancelled, asio::error::operation_aborted);
			}
			if( m_data_write_queue.empty() )
			{
				schedule();
				return ;
			}
			m_current_data = std::move(m_data_write_queue.front());
			m_data_write_queue.pop_front();

			if( m_current_data->queued_counted )
			{
				m_queued_write_operations--;
				m_queued_write_bytes -= m_current_data->queued_payload_size;
				m_current_data->queued_counted = false;
			}
		}
		start_wire_frame(std::move(m_current_data->frames[m_current_data->frame_index]),
			wire_frame_kind::data, m_current_data
		);
		return ;
	}
	// With no data pending, fairness no longer limits consecutive controls.
	m_last_wire_was_control = false;
	if( m_pending_auto_pong or m_pending_local_close or not m_control_write_queue.empty() )
		schedule();
}

void send_engine::async_write_message
(message_type type, std::span<const const_buffer> buffers, write_options options,
	std::shared_ptr<std::vector<std::byte>> payload_owner, io_handler_t completion)
{
	auto self = m_owner.send_owner();
	if( auto error = self->write_state_error() )
	{
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	auto frames = self->send_side().prepare_message(type, buffers, options);
	if( not frames )
	{
		auto error = frames.error();
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	std::shared_ptr<send_operation> operation;
	try
	{
		operation = std::make_shared<send_operation>();

		operation->kind = send_kind::data;
		operation->frames = std::move(*frames);

		operation->payload_owner = std::move(payload_owner);
		operation->completion = std::move(completion);
		operation->id = ++self->send_side().m_next_send_operation_id;

		for(const auto &frame : operation->frames)
			operation->queued_payload_size += frame.application_size;

		self->send_side().install_send_cancellation(operation);
		operation->sequence = ++self->send_side().m_last_write_sequence;

		if( auto error = self->send_side().enqueue_send_operation(operation) )
		{
			--self->send_side().m_last_write_sequence;
			self->send_side().deliver_send_completion(operation, error);
		}
	}
	catch(const std::bad_alloc&)
	{
		auto error = make_system_error_code(std::errc::not_enough_memory);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
	catch(...)
	{
		auto error = make_system_error_code(std::errc::io_error);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
}

void send_engine::async_write_data_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin,
 std::shared_ptr<std::vector<std::byte>> payload_owner, io_handler_t completion)
{
	auto self = m_owner.send_owner();
	if( auto error = self->frame_write_state_error() )
	{
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	if( self->send_side().data_busy() )
	{
		auto error = make_system_error_code(std::errc::operation_in_progress);
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	auto prepared = self->send_side().prepare_data_frame (
		type, payload, continuation, fin
	);
	if( not prepared )
	{
		auto error = prepared.error();
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	std::shared_ptr<send_operation> operation;
	try
	{
		operation = std::make_shared<send_operation>();
		operation->kind = send_kind::data;
		operation->explicit_data_frame = true;

		operation->frames.push_back(std::move(prepared->frame));
		operation->payload_owner = std::move(payload_owner);
		operation->completion = std::move(completion);

		operation->id = ++self->send_side().m_next_send_operation_id;
		operation->queued_payload_size = operation->frames.front().application_size;

		operation->next_message_type = prepared->next_message_type;
		operation->next_message_size = prepared->next_message_size;
		operation->next_utf8 = std::move(prepared->next_utf8);

		self->send_side().install_send_cancellation(operation);
		operation->sequence = ++self->send_side().m_last_write_sequence;

		if( auto error = self->send_side().enqueue_send_operation(operation) )
		{
			--self->send_side().m_last_write_sequence;
			self->send_side().deliver_send_completion(operation, error);
		}
	}
	catch(const std::bad_alloc&)
	{
		auto error = make_system_error_code(std::errc::not_enough_memory);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
	catch(...)
	{
		auto error = make_system_error_code(std::errc::io_error);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
}

size_t send_engine::write_data_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin, error_code &error) noexcept
{
	error.clear();
	if( auto state_error = m_owner.frame_write_state_error() )
	{
		error = state_error;
		return 0;
	}
	if( busy() )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return 0;
	}
	auto prepared = prepare_data_frame(type, payload, continuation, fin);
	if( not prepared )
	{
		error = prepared.error();
		return 0;
	}
	auto transferred = m_owner.write_prepared(prepared->frame, error);
	if( error )
	{
		m_owner.handle_send_failure(error);
		return transferred;
	}
	send_operation transition;
	transition.explicit_data_frame = true;

	transition.next_message_type = prepared->next_message_type;
	transition.next_message_size = prepared->next_message_size;
	transition.next_utf8 = std::move(prepared->next_utf8);

	commit_data_frame(transition);
	return transferred;
}

void send_engine::async_write_control
(opcode op, const const_buffer &payload, io_handler_t completion)
{
	auto self = m_owner.send_owner();
	if( auto error = self->write_state_error() )
	{
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	auto frame = self->send_side().prepare_control(op, payload, true);
	if( not frame )
	{
		auto error = frame.error();
		post_completion(self->send_executor(),
			std::move(completion), error, size_t{0}
		);
		return ;
	}
	std::shared_ptr<send_operation> operation;
	try {
		operation = std::make_shared<send_operation>();
		operation->kind = send_kind::application_control;

		operation->frames.push_back(std::move(*frame));
		operation->completion = std::move(completion);

		operation->id = ++self->send_side().m_next_send_operation_id;
		operation->queued_payload_size = operation->frames.front().application_size;

		self->send_side().install_send_cancellation(operation);
		if( auto error = self->send_side().enqueue_send_operation(operation) )
			self->send_side().deliver_send_completion(operation, error);
	}
	catch(const std::bad_alloc&)
	{
		auto error = make_system_error_code(std::errc::not_enough_memory);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
	catch(...)
	{
		auto error = make_system_error_code(std::errc::io_error);
		if( operation and operation->completion )
			self->send_side().deliver_send_completion(operation, error);
		else
		{
			post_completion(self->send_executor(),
				std::move(completion), error, size_t{0}
			);
		}
	}
}

size_t send_engine::write_control
(opcode op, const const_buffer &payload, error_code &error) noexcept
{
	error.clear();
	if( auto state_error = m_owner.write_state_error() )
	{
		error = state_error;
		return 0;
	}
	auto frame = m_frame_builder.prepare_control(op, payload, true);
	if( not frame )
	{
		error = frame.error();
		return 0;
	}
	auto transferred = m_owner.write_prepared(*frame, error);
	if( error )
		m_owner.handle_send_failure(error);
	return transferred;
}


void send_engine::wait_written(error_code &error) noexcept
{
	if( m_completed_write_sequence < m_last_write_sequence )
	{
		error = make_system_error_code(std::errc::operation_in_progress);
		return ;
	}
	error = observe_write_error(m_last_write_sequence);
}

void send_engine::async_wait_written(void_handler_t completion)
{
	const auto target = m_last_write_sequence;
	if( target <= m_completed_write_sequence )
	{
		auto error = observe_write_error(target);
		post_completion(m_owner.send_executor(),
			std::move(completion), error
		);
		return ;
	}
	std::shared_ptr<write_waiter> waiter;
	bool queued = false;
	try {
		auto associated_allocator = asio::get_associated_allocator(completion);

		using waiter_allocator_t = std::allocator_traits
			<decltype(associated_allocator)>::template rebind_alloc<write_waiter>;

		waiter = std::allocate_shared<write_waiter>(
			waiter_allocator_t(associated_allocator)
		);
		waiter->id = ++m_next_write_waiter_id;
		waiter->target = target;
		waiter->completion = std::move(completion);

		m_write_waiters.push_back(waiter);
		queued = true;

		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion); slot.is_connected() )
		{
			slot.assign([weak = m_owner.weak_send_owner(), id = waiter->id]
			(asio::cancellation_type type) noexcept
			{
				if( type == asio::cancellation_type::none )
					return ;

				if( auto self = weak.lock() )
				{
					try {
						asio::dispatch(self->send_executor(), [self, id]{
							self->send_side().cancel_write_waiter(id);
						});
					}
					catch(...) {}
				}
			});
		}
	}
	catch(const std::bad_alloc&)
	{
		auto error = make_system_error_code(std::errc::not_enough_memory);
		if( waiter )
		{
			if( queued )
			{
				auto it = std::ranges::find(m_write_waiters, waiter);
				if( it != m_write_waiters.end() )
					m_write_waiters.erase(it);
			}
			deliver_write_waiter(waiter, error);
		}
		else
		{
			post_completion(m_owner.send_executor(),
				std::move(completion), error
			);
		}
	}
	catch(...)
	{
		auto error = make_system_error_code(std::errc::io_error);
		if( waiter )
		{
			if( queued )
			{
				auto it = std::ranges::find(m_write_waiters, waiter);
				if( it != m_write_waiters.end() )
					m_write_waiters.erase(it);
			}
			deliver_write_waiter(waiter, error);
		}
		else
		{
			post_completion(m_owner.send_executor(),
				std::move(completion), error
			);
		}
	}
}

void send_engine::complete_wire_frame(const prepared_frame &frame, wire_frame_kind kind,
	const std::shared_ptr<send_operation> &operation, error_code error, size_t wire_size) noexcept
{
	m_wire_write_active = false;
	const bool shutdown = not m_owner.send_transport_ready();

	const auto payload_size = wire_size > frame.header_size ?
		std::min(frame.payload_size, wire_size - frame.header_size) : 0;

	if( not error and wire_size != frame.header_size + frame.payload_size )
		error = make_system_error_code(std::errc::io_error);

	if( operation )
	{
		if( not error )
			operation->transferred += frame.application_size;

		else if( frame.application_size == frame.payload_size )
			operation->transferred += payload_size;
	}
	if( shutdown and not error )
		error = asio::error::operation_aborted;

	if( error )
	{
		const auto completion_error = m_owner.protocol_failure_error(error);
		if( operation )
		{
			if( operation->kind == send_kind::data )
				m_current_data.reset();
			complete_send_operation(operation, completion_error);
		}
		if( m_current_data )
		{
			auto data = std::exchange(m_current_data, {});
			complete_send_operation(data, completion_error);
		}
		fail_queued_writes(completion_error);
		m_pending_auto_pong.reset();
		m_pending_close_response.reset();

		if( not shutdown )
			m_owner.handle_send_failure(error);
		return ;
	}
	switch(kind)
	{
	case wire_frame_kind::data:
		m_last_wire_was_control = false;
		operation->frame_index++;

		if( m_owner.protocol_failure_active() )
		{
			m_current_data.reset();
			complete_send_operation(operation,
				operation->frame_index == operation->frames.size() ?
				error_code{} : m_owner.protocol_failure_error({})
			);
			schedule();
			return ;
		}
		if( operation->frame_index == operation->frames.size() )
		{
			m_current_data.reset();
			complete_send_operation(operation, {});
		}
		break;

	case wire_frame_kind::application_control:
		m_last_wire_was_control = true;
		complete_send_operation(operation, {});
		break;

	case wire_frame_kind::auto_pong:
		m_last_wire_was_control = true;
		break;

	case wire_frame_kind::local_close:
	case wire_frame_kind::close_response:
	case wire_frame_kind::protocol_close:
		m_last_wire_was_control = true;
		m_owner.handle_wire_frame_sent(kind);
		return ;
	}
	schedule();
}

bool send_engine::queue_has_capacity(send_kind kind, size_t payload_size) const noexcept
{
	if( m_max_queued_write_operations == 0 or m_max_queued_write_bytes == 0 )
		return false;

	if( m_queued_write_operations >= m_max_queued_write_operations )
		return false;

	if( kind == send_kind::data and
		payload_size > m_max_queued_write_bytes -
			std::min(m_queued_write_bytes, m_max_queued_write_bytes) )
		return false;
	return true;
}

void send_engine::remember_write_error(uint64_t sequence, error_code error) noexcept
{
	if( error and not m_unobserved_write_error )
		m_unobserved_write_error = std::pair{sequence, error};
}

error_code send_engine::observe_write_error(uint64_t target) noexcept
{
	if( m_unobserved_write_error and m_unobserved_write_error->first <= target )
	{
		auto error = m_unobserved_write_error->second;
		m_unobserved_write_error.reset();
		return error;
	}
	return {};
}

void send_engine::complete_write_waiters()
{
	if( m_current_data )
		return ;

	while( not m_write_waiters.empty() and
		   m_write_waiters.front()->target <= m_completed_write_sequence )
	{
		auto waiter = std::move(m_write_waiters.front());
		m_write_waiters.pop_front();

		auto error = observe_write_error(waiter->target);
		deliver_write_waiter(waiter, error);
	}
}

void send_engine::deliver_write_waiter
(const std::shared_ptr<write_waiter> &waiter, error_code error, bool clear_slot) noexcept
{
	if( not waiter or not waiter->completion )
		return ;

	if( clear_slot )
	{
		if( auto slot = asio::get_associated_cancellation_slot(waiter->completion);
			slot.is_connected() )
			slot.clear();
	}
	try {
		auto completion = std::move(waiter->completion);
		std::move(completion)(error);
	}
	catch(...) {}
}

void send_engine::cancel_write_waiter(uint64_t id) noexcept
{
	for(auto it=m_write_waiters.begin(); it!=m_write_waiters.end(); ++it)
	{
		if( (*it)->id != id )
			continue;

		auto waiter = std::move(*it);
		m_write_waiters.erase(it);

		deliver_write_waiter(waiter, asio::error::operation_aborted, false);
		return ;
	}
}

void send_engine::deliver_send_completion
(const std::shared_ptr<send_operation> &operation, error_code error) noexcept
{
	if( not operation->completion )
		return ;

	if( auto slot = asio::get_associated_cancellation_slot(operation->completion);
		slot.is_connected() )
		slot.clear();
	try {
		auto completion = std::move(operation->completion);
		std::move(completion)(error, operation->transferred);
	}
	catch(...) {}
}

void send_engine::complete_send_operation
(const std::shared_ptr<send_operation> &operation, error_code error) noexcept
{
	if( not error and operation->explicit_data_frame )
		commit_data_frame(*operation);

	if( operation->kind == send_kind::data )
	{
		m_completed_write_sequence = std::max(m_completed_write_sequence, operation->sequence);
		remember_write_error(operation->sequence, error);
	}
	deliver_send_completion(operation, error);

	if( operation->kind == send_kind::data )
		complete_write_waiters();
}

void send_engine::commit_data_frame(send_operation &operation) noexcept
{
	m_outgoing_message_type = operation.next_message_type;
	m_outgoing_message_size = operation.next_message_size;
	m_outgoing_utf8 = std::move(operation.next_utf8);
}

void send_engine::install_send_cancellation(const std::shared_ptr<send_operation> &operation)
{
	auto slot = asio::get_associated_cancellation_slot(operation->completion);
	if( not slot.is_connected() )
		return ;

	slot.assign([weak = m_owner.weak_send_owner(), id = operation->id]
	(asio::cancellation_type type) noexcept
	{
		if( type == asio::cancellation_type::none )
			return ;

		if( auto self = weak.lock() )
		{
			try {
				asio::dispatch(self->send_executor(), [self, id]{
					self->send_side().cancel_queued_send(id);
				});
			}
			catch(...) {}
		}
	});
}

void send_engine::cancel_queued_send(uint64_t id) noexcept
{
	for(auto &operation : m_data_write_queue)
	{
		if( operation->id != id or operation->cancel_requested )
			continue;

		operation->cancel_requested = true;
		deliver_send_completion(operation, asio::error::operation_aborted);
		return ;
	}
	for(auto it=m_control_write_queue.begin(); it!=m_control_write_queue.end(); ++it)
	{
		if( (*it)->id != id )
			continue;

		auto operation = std::move(*it);
		m_control_write_queue.erase(it);

		if( operation->queued_counted )
		{
			m_queued_write_operations--;
			operation->queued_counted = false;
		}
		deliver_send_completion(operation, asio::error::operation_aborted);
		return ;
	}
}

void send_engine::start_wire_frame
(prepared_frame frame, wire_frame_kind kind, std::shared_ptr<send_operation> operation) noexcept
{
	m_wire_write_active = true;
	m_owner.start_transport_write(std::move(frame), kind, std::move(operation));
}


error_code send_engine::enqueue_send_operation
(std::shared_ptr<send_operation> operation) noexcept
{
	if( not busy() )
	{
		if( operation->kind == send_kind::data )
			m_current_data = std::move(operation);
		else
		{
			auto frame = std::move(operation->frames.front());
			start_wire_frame(std::move(frame),
				wire_frame_kind::application_control, std::move(operation)
			);
			return {};
		}
		schedule();
		return {};
	}
	if( not queue_has_capacity(operation->kind, operation->queued_payload_size) )
		return make_error_code(errc::write_queue_full);
	try {
		m_queued_write_operations++;
		operation->queued_counted = true;

		if( operation->kind == send_kind::data )
		{
			m_queued_write_bytes += operation->queued_payload_size;
			m_data_write_queue.push_back(operation);
		}
		else
			m_control_write_queue.push_back(operation);
		return {};
	}
	catch(const std::bad_alloc&)
	{
		m_queued_write_operations--;
		operation->queued_counted = false;

		if( operation and operation->kind == send_kind::data )
			m_queued_write_bytes -= operation->queued_payload_size;

		return make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...)
	{
		m_queued_write_operations--;
		operation->queued_counted = false;

		if( operation and operation->kind == send_kind::data )
			m_queued_write_bytes -= operation->queued_payload_size;
	}
	return make_system_error_code(std::errc::io_error);
}

sys_expected<> send_engine::retain_protocol_payload
(optional<std::vector<std::byte>> &slot, const std::vector<std::byte> &payload) noexcept
{
	try {
		slot = payload;
		return make_sys_expected();
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

} //namespace riwo::websocket::detail
