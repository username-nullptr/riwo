// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_DUPLEX_CHANNEL_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_DUPLEX_CHANNEL_H

#ifndef RIWO_HTTP_EXP_PROTOCOL_DUPLEX_CHANNEL_H
# error "Include <riwo/http_exp/protocol/duplex_channel.h> instead."
#endif

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API duplex_state
{
	RIWO_DISABLE_COPY_MOVE(duplex_state)

public:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using close_handler_t = asio::any_completion_handler<void(error_code)>;
	using endpoint_t = asio::ip::tcp::endpoint;

	duplex_state() = default;
	virtual ~duplex_state() = default;

public:
	[[nodiscard]] virtual size_t read_some_sync(mutable_buffer, error_code&) noexcept = 0;
	virtual void read_some(mutable_buffer, io_handler_t) = 0;

	[[nodiscard]] virtual size_t write_sync(std::span<const const_buffer>, error_code&) noexcept = 0;
	virtual void write(std::span<const const_buffer>, io_handler_t) = 0;

	virtual void close_sync(error_code&) noexcept = 0;
	virtual void close(close_handler_t) = 0;
	virtual void cancel() noexcept = 0;

public:
	[[nodiscard]] virtual endpoint_t remote_endpoint(error_code&) const noexcept = 0;
	[[nodiscard]] virtual endpoint_t local_endpoint(error_code&) const noexcept = 0;

	[[nodiscard]] virtual asio::any_io_executor get_executor() const noexcept = 0;
	[[nodiscard]] virtual protocol selected_protocol() const noexcept = 0;
	[[nodiscard]] virtual bool is_open() const noexcept = 0;
};

using duplex_state_ptr = std::shared_ptr<duplex_state>;

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
basic_duplex_channel<Exec>::basic_duplex_channel()
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_duplex_channel(executor_t(riwo::get_executor()))
{

}

template <core_concepts::exec Exec>
basic_duplex_channel<Exec>::basic_duplex_channel(std::shared_ptr<detail::duplex_state> state)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_duplex_channel(executor_t(riwo::get_executor()), std::move(state))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_duplex_channel<Exec>::basic_duplex_channel(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_t> :
	m_exec(get_executor_helper(std::forward<Exec0>(executor)))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_duplex_channel<Exec>::basic_duplex_channel
(Exec0 &&executor, std::shared_ptr<detail::duplex_state> state)
	requires core_concepts::match_sched<Exec0,executor_t> :
	m_state(std::move(state)),
	m_exec(get_executor_helper(std::forward<Exec0>(executor)))
{

}

template <core_concepts::exec Exec>
basic_duplex_channel<Exec>::basic_duplex_channel(basic_duplex_channel &&other) noexcept :
	m_state(std::move(other.m_state)), m_exec(other.m_exec)
{

}

template <core_concepts::exec Exec>
auto basic_duplex_channel<Exec>::operator=(basic_duplex_channel &&other) noexcept
	-> basic_duplex_channel&
{
	if( this == &other )
		return *this;

	if( m_state and m_state->is_open() )
		m_state->cancel();

	m_exec = other.m_exec;
	m_state = std::move(other.m_state);
	return *this;
}

template <core_concepts::exec Exec>
basic_duplex_channel<Exec>::~basic_duplex_channel()
{
	if( m_state and m_state->is_open() )
		m_state->cancel();
}

template <core_concepts::exec Exec>
auto basic_duplex_channel<Exec>::get_executor() const noexcept -> executor_t
{
	return m_exec;
}

template <core_concepts::exec Exec>
protocol basic_duplex_channel<Exec>::selected_protocol() const noexcept
{
	return m_state ?
		m_state->selected_protocol() : protocol::v1_1;
}

template <core_concepts::exec Exec>
bool basic_duplex_channel<Exec>::is_open() const noexcept
{
	return m_state and m_state->is_open();
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,size_t> Token>
auto basic_duplex_channel<Exec>::read_some(mutable_buffer buffer, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return m_state ?
			m_state->read_some_sync(buffer, token) :
			(token = make_error_code(errc::operation_not_permitted), size_t{});
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto size = m_state ?
			m_state->read_some_sync(buffer, error) : 0;

		if( not m_state )
			error = make_error_code(errc::operation_not_permitted);

		return expected_value_or_throw(error ?
			sys_expected<size_t>(sys_unexpected(error)) :
			sys_expected<size_t>(size));
	}
	else
	{
		auto state = m_state;
		return initiate_io<size_t>(m_exec,
		[state = std::move(state), buffer](auto handler) mutable
		{
			if( not state )
			{
				return std::move(handler) (
					make_error_code(errc::operation_not_permitted), 0
				);
			}
			state->read_some(buffer,
				detail::duplex_state::io_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_duplex_channel<Exec>::write(const const_buffer &buffer, Token &&token)
{
	const std::array buffers {buffer};
	return write(std::span<const const_buffer>(buffers), std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_duplex_channel<Exec>::write(std::span<const const_buffer> buffers, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return m_state ?
			m_state->write_sync(buffers, token) :
			(token = make_error_code(errc::operation_not_permitted), size_t{});
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto size = m_state ?
			m_state->write_sync(buffers, error) : 0;

		if( not m_state )
			error = make_error_code(errc::operation_not_permitted);

		return expected_value_or_throw(error ?
			sys_expected<size_t>(sys_unexpected(error)) :
			sys_expected<size_t>(size));
	}
	else
	{
		auto state = m_state;
		using token_t = std::remove_cvref_t<Token>;

		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			size_t total = 0;
			for(const auto &buffer : buffers)
				total += buffer.size();

			auto bytes = std::make_shared<std::vector<std::byte>>(total);
			size_t offset = 0;

			for(const auto &buffer : buffers)
			{
				if( buffer.size() != 0 )
					std::memcpy(bytes->data() + offset, buffer.data(), buffer.size());
				offset += buffer.size();
			}
			return initiate_io<size_t>(m_exec,
			[state = std::move(state), bytes](auto handler) mutable
			{
				if( not state )
				{
					return std::move(handler) (
						make_error_code(errc::operation_not_permitted), 0
					);
				}
				const std::array<const_buffer,1> owned {
					buffer(*bytes)
				};
				state->write(owned, detail::duplex_state::io_handler_t(
				[bytes, handler = std::move(handler)](error_code error, size_t size) mutable {
					std::move(handler)(error, size);
				}));
			},
			std::forward<Token>(token));
		}
		else
		{
			// Buffer descriptors are copied because deferred completion tokens may
			// invoke the initiation after this function returns. The described byte
			// ranges remain borrowed from the caller until completion.
			auto descriptors = std::make_shared
				<std::vector<const_buffer>>(buffers.begin(), buffers.end());

			return initiate_io<size_t>(m_exec,
			[state = std::move(state), descriptors](auto handler) mutable
			{
				if( not state )
				{
					return std::move(handler) (
						make_error_code(errc::operation_not_permitted), 0
					);
				}
				state->write(*descriptors, detail::duplex_state::io_handler_t(
				[descriptors, handler = std::move(handler)](error_code error, size_t size) mutable {
					std::move(handler)(error, size);
				}));
			},
			std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code> Token>
auto basic_duplex_channel<Exec>::close(Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
	{
		if( m_state )
			m_state->close_sync(token);
		else
			token = make_error_code(errc::operation_not_permitted);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		if( m_state )
			m_state->close_sync(error);
		else
			error = make_error_code(errc::operation_not_permitted);

		if( error )
		{
			(void)expected_value_or_throw (
				sys_expected<std::monostate>(sys_unexpected(error))
			);
		}
	}
	else
	{
		auto state = m_state;
		return initiate_io_void(m_exec,
		[state = std::move(state)](auto handler) mutable
		{
			if( not state )
			{
				return std::move(handler) (
					make_error_code(errc::operation_not_permitted)
				);
			}
			state->close(detail::duplex_state::close_handler_t(std::move(handler)));
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_duplex_channel<Exec>::cancel() noexcept
{
	if( m_state )
		m_state->cancel();
}

template <core_concepts::exec Exec>
auto basic_duplex_channel<Exec>::remote_endpoint(error_code &error) const noexcept
	-> endpoint_t
{
	if( m_state )
		return m_state->remote_endpoint(error);

	error = make_error_code(errc::operation_not_permitted);
	return {};
}

template <core_concepts::exec Exec>
auto basic_duplex_channel<Exec>::local_endpoint(error_code &error) const noexcept
	-> endpoint_t
{
	if( m_state )
		return m_state->local_endpoint(error);

	error = make_error_code(errc::operation_not_permitted);
	return {};
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_DUPLEX_CHANNEL_H
