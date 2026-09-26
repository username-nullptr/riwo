// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_H

#include <riwo/websocket/detail/stream/impl.h>

#ifndef RIWO_WEBSOCKET_STREAM_H
# error "Include <riwo/websocket/stream.h> instead."
#endif //RIWO_WEBSOCKET_STREAM_H

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
# error "The stream implementation must be declared before this fragment."
#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H

namespace riwo::websocket
{

template <core_concepts::exec Exec>
basic_stream<Exec>::basic_stream()
	requires core_concepts::match_sched<io_executor_t, executor_t> :
	basic_stream(executor_t(riwo::get_executor()))
{

}

template <core_concepts::exec Exec>
basic_stream<Exec>::basic_stream(config_t config)
	requires core_concepts::match_sched<io_executor_t, executor_t> :
	basic_stream(executor_t(riwo::get_executor()), config)
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_stream<Exec>::basic_stream(Exec0 &&exec, config_t config) requires
(not std::same_as<std::remove_cvref_t<Exec0>,basic_stream> and core_concepts::match_sched<Exec0,executor_t>):
	m_impl(std::make_shared<impl>(executor_t(get_executor_helper(std::forward<decltype(exec)>(exec))), config))
{

}

template <core_concepts::exec Exec>
basic_stream<Exec>::basic_stream(basic_stream &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::operator=(basic_stream &&other) noexcept
{
	if( this == &other )
		return *this;

	if( m_impl )
	{
		error_code error;
		m_impl->shutdown(error);
	}
	m_impl = std::move(other.m_impl);
	return *this;
}

template <core_concepts::exec Exec>
basic_stream<Exec>::~basic_stream()
{
	if( m_impl )
	{
		error_code error;
		m_impl->shutdown(error);
	}
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::adopt(connection_ptr connection, adopt_options_t options)
{
	error_code error;
	m_impl->adopt(std::move(connection), std::move(options), error);
	if( error )
		system_error::loc_throw(error, "riwo::websocket::basic_stream::adopt");
	return *this;
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::adopt
(connection_ptr connection, adopt_options_t options, error_code &error) noexcept
{
	m_impl->adopt(std::move(connection), std::move(options), error);
	return *this;
}

template <core_concepts::exec Exec>
template <typename Buffer, typename Token>
auto basic_stream<Exec>::read(Token &&token) requires
	concepts::buffer<Buffer> and task_token_v<Token,basic_message<Buffer>>
{
	using buffer_t = std::remove_cvref_t<Buffer>;
	using result_t = basic_message<buffer_t>;

	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto &error = adapted_error.get();

		auto value = m_impl->read(error);
		if( error )
			return result_t{};

		return impl::template convert_message<buffer_t>(std::move(value), error);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto value = m_impl->read(error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::read");

		auto result = impl::template convert_message<buffer_t>(
			std::move(value), error
		);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::read");
		return result;
	}
	else
	{
		return initiate_io<result_t>(get_executor(), [self = m_impl]<typename T0>(T0 &&completion_token) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_token);
			auto completion_exec = asio::get_associated_executor(completion_token, self->m_exec);
			auto allocator = asio::get_associated_allocator(completion_token);

			auto bridge = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
				asio::bind_cancellation_slot(slot, [handler = std::forward<T0>(completion_token)]
				(error_code error, message value) mutable
				{
					if( error )
					{
						std::move(handler)(error, result_t {});
						return;
					}
					auto result = impl::template convert_message<buffer_t>(
						std::move(value), error
					);
					std::move(handler)(error, std::move(result));
				}))
			);
			self->async_read_message(std::move(bridge));
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Buffer, typename Token>
auto basic_stream<Exec>::read_frame(Token &&token) requires
	concepts::buffer<Buffer> and task_token_v<Token,basic_data_frame<Buffer>>
{
	using buffer_t = std::remove_cvref_t<Buffer>;
	using result_t = basic_data_frame<buffer_t>;

	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto &error = adapted_error.get();

		auto value = m_impl->read_frame(error);
		if( error )
			return result_t{};

		return impl::template convert_frame<buffer_t>(std::move(value), error);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto value = m_impl->read_frame(error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::read_frame");

		auto result = impl::template convert_frame<buffer_t>(
			std::move(value), error
		);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::read_frame");
		return result;
	}
	else
	{
		return initiate_io<result_t>(get_executor(),
		[self = m_impl]<typename T0>(T0 &&completion_token) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_token);
			auto completion_exec = asio::get_associated_executor(completion_token, self->m_exec);
			auto allocator = asio::get_associated_allocator(completion_token);

			auto bridge = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
				asio::bind_cancellation_slot(slot,
				[handler = std::forward<T0>(completion_token)]
				(error_code error, data_frame value) mutable
				{
					if( error )
					{
						std::move(handler)(error, result_t {});
						return;
					}
					auto result = impl::template convert_frame<buffer_t>(
						std::move(value), error
					);
					std::move(handler)(error, std::move(result));
				}))
			);
			self->async_read_frame(std::move(bridge));
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Consumer, typename Token>
auto basic_stream<Exec>::consume(Consumer &&consumer, Token &&token) requires (
	std::invocable<std::remove_reference_t<Consumer>&,const message_chunk_t&> and
	std::same_as<std::invoke_result_t<std::remove_reference_t<Consumer>&,const message_chunk_t&>,void> and
	task_token_v<Token,message_info_t>
){
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->consume(consumer, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->consume(consumer, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::consume");
		return result;
	}
	else
	{
		using consumer_t = std::remove_cvref_t<Consumer>;
		return initiate_io<message_info_t>(get_executor(),
		[self = m_impl, consumer = consumer_t(std::forward<Consumer>(consumer))]
		<typename T0>(T0 &&completion_token) mutable
		{
			auto slot = asio::get_associated_cancellation_slot(completion_token);
			auto completion_exec = asio::get_associated_executor(completion_token, self->m_exec);
			auto allocator = asio::get_associated_allocator(completion_token);

			auto bridge = asio::bind_allocator(allocator, asio::bind_executor(completion_exec,
				asio::bind_cancellation_slot(slot,
				[handler = std::forward<T0>(completion_token)]
				(error_code error, message_info_t info) mutable {
					std::move(handler)(error, info);
				}))
			);
			self->async_consume(std::move(consumer), std::move(bridge));
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <message_type Type, typename Token>
auto basic_stream<Exec>::write(const const_buffer &body, Token &&token)
	requires completion_token_v<Token, size_t>
{
	static_assert(Type == message_type::text or Type == message_type::binary);
	return write(Type, body, write_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write(message_type type, const const_buffer &body, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write(type, std::span(&body, 1), write_options{},
		std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write(message_type type, std::span<const const_buffer> body, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write(type, body, write_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write_text(std::string_view text, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write_text(text, write_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write_binary(const const_buffer &body, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write_binary(body, write_options{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Buffer, typename Token>
auto basic_stream<Exec>::write_frame(const basic_data_frame<Buffer> &frame, Token &&token)
	requires concepts::buffer<Buffer> and completion_token_v<Token,size_t>
{
	const const_buffer body(riwo::buffer(frame.body));
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->write_frame(frame.type, body,
			frame.continuation, frame.fin, adapted_error.get()
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto transferred = m_impl->write_frame(frame.type, body,
			frame.continuation, frame.fin, error
		);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::write_frame");
		return transferred;
	}
	else
	{
		std::shared_ptr<std::vector<std::byte>> payload_owner;
		error_code buffer_error {};
		const_buffer payload = body;

		if constexpr( is_detached_v<token_unbound_t<Token>> )
		{
			try {
				if( const auto *data = static_cast<const std::byte*>(body.data());
					body.size() != 0 and data == nullptr )
					buffer_error = make_system_error_code(std::errc::invalid_argument);
				else
				{
					payload_owner = std::make_shared<std::vector<std::byte>>(body.size());
					if( body.size() != 0 )
						std::memcpy(payload_owner->data(), data, body.size());
					payload = const_buffer(payload_owner->data(), payload_owner->size());
				}
			}
			catch(const std::bad_alloc&) {
				buffer_error = make_system_error_code(std::errc::not_enough_memory);
			}
			catch(...) {
				buffer_error = make_system_error_code(std::errc::io_error);
			}
		}
		return initiate_io<size_t>(get_executor(), [self = m_impl,
			type = frame.type, continuation = frame.continuation, fin = frame.fin,
			payload, payload_owner = std::move(payload_owner), buffer_error
		]<typename T0>(T0 &&completion_token) mutable
		{
			if( buffer_error )
			{
				riwo::post_completion(self->m_exec,
					std::forward<T0>(completion_token), buffer_error, size_t{0}
				);
				return ;
			}
			self->async_write_frame(type, payload, continuation, fin,
				std::move(payload_owner), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::wait_written(Token &&token)
	requires task_token_v<Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		m_impl->wait_written(adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->wait_written(error);
		if( error )
		{
			system_error::loc_throw(error,
				"riwo::websocket::basic_stream::wait_written"
			);
		}
	}
	else
	{
		return initiate_io_void(get_executor(),
			[self = m_impl]<typename T0>(T0 &&completion_token) mutable {
				self->async_wait_written(std::forward<T0>(completion_token));
			}, std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
template <message_type Type, typename Token>
auto basic_stream<Exec>::write(const const_buffer &body, write_options options, Token &&token)
	requires completion_token_v<Token, size_t>
{
	static_assert(Type == message_type::text or Type == message_type::binary);
	return write(Type, body, options, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write
(message_type type, const const_buffer &body, write_options options, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write(type, std::span(&body, 1), options,
		std::forward<Token>(token)
	);
}


template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write
(message_type type, std::span<const const_buffer> body, write_options options, Token &&token)
	requires completion_token_v<Token, size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->write(type, body, options, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto transferred = m_impl->write(type, body, options, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::write");
		return transferred;
	}
	else
	{
		auto buffers = std::vector(body.begin(), body.end());
		std::shared_ptr<std::vector<std::byte>> payload_owner;
		error_code buffer_error {};

		if constexpr( is_detached_v<token_unbound_t<Token>> )
		{
			try {
				size_t size = 0;
				for(const auto &input : buffers)
				{
					if( input.size() != 0 and input.data() == nullptr )
					{
						buffer_error = make_system_error_code(std::errc::invalid_argument);
						break;
					}
					if( input.size() > std::numeric_limits<size_t>::max() - size )
					{
						buffer_error = make_system_error_code(std::errc::value_too_large);
						break;
					}
					size += input.size();
				}
				if( not buffer_error )
				{
					payload_owner = std::make_shared<std::vector<std::byte>>(size);
					size_t offset = 0;

					for(const auto &input : buffers)
					{
						if( input.size() == 0 )
							continue;

						std::memcpy(payload_owner->data() + offset, input.data(), input.size());
						offset += input.size();
					}
					buffers.assign(1, const_buffer (
						payload_owner->data(), payload_owner->size()
					));
				}
				else
					buffers.clear();
			}
			catch(const std::bad_alloc&)
			{
				buffer_error = make_system_error_code(std::errc::not_enough_memory);
				buffers.clear();
			}
			catch(...)
			{
				buffer_error = make_system_error_code(std::errc::io_error);
				buffers.clear();
			}
		}
		return initiate_io<size_t>(get_executor(), [self = m_impl, type, options,
			buffers = std::move(buffers), payload_owner = std::move(payload_owner), buffer_error
		]<typename T0>(T0 &&completion_token) mutable
		{
			if( buffer_error )
			{
				riwo::post_completion(self->m_exec,
					std::forward<T0>(completion_token), buffer_error, size_t{0}
				);
				return ;
			}
			self->async_write_message(type, buffers, options, std::move(payload_owner),
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write_text(std::string_view text, write_options options, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write(message_type::text, const_buffer(text.data(), text.size()),
		options, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::write_binary(const const_buffer &body, write_options options, Token &&token)
	requires completion_token_v<Token, size_t>
{
	return write(message_type::binary, body, options, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Func>
basic_stream<Exec> &basic_stream<Exec>::on_ping(Func &&callback)
	requires control_callback_v<Func>
{
	using callback_t = std::remove_cvref_t<Func>;
	using result_t = std::invoke_result_t<callback_t&,ctrl_payload_t&>;

	if constexpr( is_awaitable_v<result_t> )
	{
		m_impl->on_ping(typename impl::async_control_callback_t(
		[callback = callback_t(std::forward<Func>(callback))]
		(ctrl_payload_t &payload) mutable -> awaitable<void> {
			static_cast<void>(co_await std::invoke(callback, payload));
		}));
	}
	else
	{
		m_impl->on_ping(typename impl::sync_control_callback_t(
		[callback = callback_t(std::forward<Func>(callback))]
		(ctrl_payload_t &payload) mutable {
			static_cast<void>(std::invoke(callback, payload));
		}));
	}
	return *this;
}

template <core_concepts::exec Exec>
template <typename Func>
basic_stream<Exec> &basic_stream<Exec>::on_pong(Func &&callback)
	requires control_callback_v<Func>
{
	using callback_t = std::remove_cvref_t<Func>;
	using result_t = std::invoke_result_t<callback_t&,ctrl_payload_t&>;
	if constexpr( is_awaitable_v<result_t> )
	{
		m_impl->on_pong(typename impl::async_control_callback_t(
		[callback = callback_t(std::forward<Func>(callback))]
		(ctrl_payload_t &payload) mutable -> awaitable<void>
		{
			static_cast<void>(co_await std::invoke(callback, payload));
		}));
	}
	else
	{
		m_impl->on_pong(typename impl::sync_control_callback_t(
		[callback = callback_t(std::forward<Func>(callback))]
		(ctrl_payload_t &payload) mutable
		{
			static_cast<void>(std::invoke(callback, payload));
		}));
	}
	return *this;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::ping(Token &&token)
	requires task_token_v<Token, size_t>
{
	return ping(const_buffer{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::ping(const const_buffer &payload, Token &&token)
	requires task_token_v<Token, size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->write_control(opcode::ping, payload, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto transferred = m_impl->write_control(opcode::ping, payload, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::ping");
		return transferred;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
			[self = m_impl, payload]<typename T0>(T0 &&completion_token) mutable {
				self->async_write_control(opcode::ping, payload, std::forward<T0>(completion_token));
			}, std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::pong(Token &&token)
	requires task_token_v<Token, size_t>
{
	return pong(const_buffer{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::pong(const const_buffer &payload, Token &&token)
	requires task_token_v<Token, size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->write_control(opcode::pong, payload, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto transferred = m_impl->write_control(opcode::pong, payload, error);
		if( error )
			system_error::loc_throw(error, "riwo::websocket::basic_stream::pong");
		return transferred;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
			[self = m_impl, payload]<typename T0>(T0 &&completion_token) mutable {
				self->async_write_control(opcode::pong, payload, std::forward<T0>(completion_token));
			}, std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::on_closed(closed_callback_t callback)
{
	m_impl->on_closed(std::move(callback));
	return *this;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::wait_closed(Token &&token)
	requires task_token_v<Token, close_info_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->wait_closed(adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->wait_closed(error);
		if( error )
		{
			system_error::loc_throw(error,
				"riwo::websocket::basic_stream::wait_closed"
			);
		}
		return result;
	}
	else
	{
		return initiate_io<close_info_t>(get_executor(),
			[self = m_impl]<typename T0>(T0 &&completion_token) mutable {
				self->async_wait_closed(std::forward<T0>(completion_token));
			}, std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::close(Token &&token)
	requires completion_token_v<Token, close_info_t>
{
	return close(close_frame_t{}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_stream<Exec>::close(close_frame_t frame, Token &&token)
	requires completion_token_v<Token, close_info_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->close(std::move(frame), adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->close(std::move(frame), error);
		if( error )
		{
			system_error::loc_throw(error,
				"riwo::websocket::basic_stream::close"
			);
		}
		return result;
	}
	else
	{
		return initiate_io<close_info_t>(get_executor(),
			[self = m_impl, frame = std::move(frame)]<typename T0>(T0 &&completion_token) mutable {
				self->async_close(std::move(frame), std::forward<T0>(completion_token));
			}, std::forward<Token>(token)
		);
	}
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::cancel()
{
	error_code error;
	cancel(error);
	if( error )
		system_error::loc_throw(error, "riwo::websocket::basic_stream::cancel");
	return *this;
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::cancel(error_code &error) noexcept
{
	m_impl->cancel(error);
	return *this;
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::shutdown()
{
	error_code error;
	shutdown(error);
	if( error )
		system_error::loc_throw(error, "riwo::websocket::basic_stream::shutdown");
	return *this;
}

template <core_concepts::exec Exec>
basic_stream<Exec> &basic_stream<Exec>::shutdown(error_code &error) noexcept
{
	m_impl->shutdown(error);
	return *this;
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::get_executor() const noexcept -> executor_t
{
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
connection_state basic_stream<Exec>::state() const noexcept
{
	return m_impl ? m_impl->m_state : connection_state::closed;
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::is_open() const noexcept
{
	return state() == connection_state::open;
}

template <core_concepts::exec Exec>
bool basic_stream<Exec>::is_closing() const noexcept
{
	return state() == connection_state::closing;
}

template <core_concepts::exec Exec>
role basic_stream<Exec>::stream_role() const noexcept
{
	return m_impl ? m_impl->m_role : role::client;
}

template <core_concepts::exec Exec>
std::string basic_stream<Exec>::negotiated_subprotocol() const
{
	return m_impl ? m_impl->m_subprotocol : std::string{};
}

template <core_concepts::exec Exec>
std::vector<extension> basic_stream<Exec>::negotiated_extensions() const
{
	return m_impl ? m_impl->m_extensions : std::vector<extension>{};
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::config() const noexcept -> config_t
{
	return m_impl ? m_impl->m_config : config_t{};
}

template <core_concepts::exec Exec>
auto basic_stream<Exec>::peer_close() const -> optional<close_info_t>
{
	return m_impl ? m_impl->m_peer_close : nullopt;
}

template <core_concepts::exec Exec>
http::endpoint basic_stream<Exec>::remote_endpoint() const noexcept
{
	return m_impl and m_impl->m_connection ?
		m_impl->m_connection->remote_endpoint() : http::endpoint{};
}

template <core_concepts::exec Exec>
http::endpoint basic_stream<Exec>::local_endpoint() const noexcept
{
	return m_impl and m_impl->m_connection ?
		m_impl->m_connection->local_endpoint() : http::endpoint{};
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_H
