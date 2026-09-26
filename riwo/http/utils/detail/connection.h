// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_DETAIL_CONNECTION_H
#define RIWO_HTTP_UTILS_DETAIL_CONNECTION_H

#include <riwo/core/async_expected.h>

namespace riwo::http { namespace detail
{

class RIWO_HTTP_API const_buffer_sequence
{
	static constexpr size_t inline_capacity = 4;

public:
	using value_type = const_buffer;
	using const_iterator = const value_type*;

	explicit const_buffer_sequence(std::span<const const_buffer> buffers);
	[[nodiscard]] std::span<const const_buffer> buffers() const noexcept;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

private:
	std::array<const_buffer,inline_capacity> m_inline {};
	std::vector<const_buffer> m_dynamic {};
	size_t m_size = 0;
};

template <typename Token, typename Initiation>
[[nodiscard]] RIWO_HTTP_TAPI auto initiate_connection_io(Initiation initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t ntoken(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code,size_t)>(
		std::move(initiation), ntoken
	);
}

struct connection_io_completion
{
	error_code error {};
	size_t transferred = 0;
};

template <bool OwnsBuffer = false, typename Exec, typename Token, typename Initiation>
[[nodiscard]] RIWO_HTTP_TAPI auto initiate_connection_io
(const Exec &exec, Initiation initiation, Token &&token, std::shared_ptr<void> owner = {})
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_redirect_time_v<token_t> )
	{
		token_t timed_token(std::forward<Token>(token));
		using completion_token_t = std::remove_cvref_t<decltype(timed_token.token)>;

		completion_token_t completion_token(std::move(timed_token.token));
		auto timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(
			timed_token.time
		);
		if( timeout <= std::chrono::nanoseconds::zero() )
		{
			if constexpr( OwnsBuffer )
			{
				auto owned_token = asio::consign(std::move(completion_token), std::move(owner));
				return initiate_connection_io(std::move(initiation), std::move(owned_token));
			}
			else
			{
				return initiate_connection_io(std::move(initiation),
					std::move(completion_token)
				);
			}
		}
		return asio::async_initiate<completion_token_t,void(error_code,size_t)>(
		[exec, io_initiation = std::move(initiation), timeout, buffer_owner = std::move(owner)]
		(auto completion_handler) mutable
		{
			if constexpr( OwnsBuffer )
			{
				auto owned_handler = asio::consign (
					std::move(completion_handler), std::move(buffer_owner)
				);
				riwo::detail::start_timed_io<size_t>(exec,
					std::move(io_initiation), timeout, std::move(owned_handler)
				);
			}
			else
			{
				riwo::detail::start_timed_io<size_t>(exec,
					std::move(io_initiation), timeout, std::move(completion_handler)
				);
			}
		},
		completion_token);
	}
	else if constexpr( OwnsBuffer )
	{
		auto owned_token = asio::consign (
			std::forward<Token>(token), std::move(owner)
		);
		return initiate_connection_io (
			std::move(initiation), std::move(owned_token)
		);
	}
	else
	{
		return initiate_connection_io (
			std::move(initiation), std::forward<Token>(token)
		);
	}
}

[[nodiscard]] RIWO_HTTP_API
std::shared_ptr<std::string> copy_buffer(const const_buffer &buffer);

[[nodiscard]] RIWO_HTTP_API
std::shared_ptr<std::string> copy_buffers(std::span<const const_buffer> buffers);

template <typename Socket>
[[nodiscard]] RIWO_HTTP_TAPI
sys_expected<connection_probe_state> probe_tcp_socket(Socket &socket) noexcept
{
	if( not socket.is_open() )
		return connection_probe_state::peer_closed;

	error_code error {};
	bool was_non_blocking = socket.non_blocking();

	socket.non_blocking(true, error);
	if( error )
		return sys_unexpected(error);

	char byte = 0;
	auto size = socket.receive(asio::buffer(&byte, 1),
		asio::socket_base::message_peek, error
	);
	error_code restore_error {};
	socket.non_blocking(was_non_blocking, restore_error);

	if( restore_error )
		return sys_unexpected(restore_error);

	if( not error )
	{
		return size == 0 ?
			connection_probe_state::peer_closed :
			connection_probe_state::data_pending;
	}
	if( error == errc::would_block or error == errc::try_again )
		return connection_probe_state::no_event;

	if( error == errc::eof or error == errc::connection_reset or
		error == errc::connection_aborted or error == errc::not_connected or
		error == errc::bad_descriptor )
		return connection_probe_state::peer_closed;

	return sys_unexpected(error);
}

[[nodiscard]] RIWO_HTTP_API
endpoint to_endpoint(const asio::ip::tcp::endpoint &value) noexcept;

template <typename Socket>
[[nodiscard]] RIWO_HTTP_TAPI
sys_expected<> set_tcp_socket_options(Socket &socket, const tcp_socket_options &options) noexcept
{
	error_code error {};
	auto set = [&socket, &error](const auto &option)
	{
		socket.set_option(option, error);
		return not error;
	};
	if( options.no_delay and not set(asio::ip::tcp::no_delay(*options.no_delay)) )
		return sys_unexpected(error);

	if( options.keep_alive and not set(asio::socket_base::keep_alive(*options.keep_alive)) )
		return sys_unexpected(error);

	if( options.send_buffer_size )
	{
		if( *options.send_buffer_size > static_cast<size_t>(std::numeric_limits<int>::max()) )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( not set(asio::socket_base::send_buffer_size(static_cast<int>(*options.send_buffer_size))) )
			return sys_unexpected(error);
	}
	if( options.receive_buffer_size )
	{
		if( *options.receive_buffer_size > static_cast<size_t>(std::numeric_limits<int>::max()) )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( not set(asio::socket_base::receive_buffer_size(static_cast<int>(*options.receive_buffer_size))) )
			return sys_unexpected(error);
	}
	if( options.linger and not set(*options.linger) )
		return sys_unexpected(error);
	return make_sys_expected();
}

template <typename Socket>
[[nodiscard]] RIWO_CORE_TAPI
sys_expected<tcp_socket_state> get_tcp_socket_options(const Socket &socket) noexcept
{
	tcp_socket_state state {};
	error_code error {};

	asio::ip::tcp::no_delay no_delay {};
	socket.get_option(no_delay, error);
	if( error )
		return sys_unexpected(error);
	state.no_delay = no_delay.value();

	asio::socket_base::keep_alive keep_alive {};
	socket.get_option(keep_alive, error);
	if( error )
		return sys_unexpected(error);
	state.keep_alive = keep_alive.value();

	asio::socket_base::send_buffer_size send_size {};
	socket.get_option(send_size, error);
	if( error )
		return sys_unexpected(error);
	state.send_buffer_size = static_cast<size_t>(send_size.value());

	asio::socket_base::receive_buffer_size receive_size {};
	socket.get_option(receive_size, error);
	if( error )
		return sys_unexpected(error);
	state.receive_buffer_size = static_cast<size_t>(receive_size.value());

	socket.get_option(state.linger, error);
	if( error )
		return sys_unexpected(error);
	return state;
}

} //namespace detail

template <core_concepts::exec Exec>
basic_connection<Exec>::~basic_connection() = default;

template <core_concepts::exec Exec>
template <typename Token>
auto basic_connection<Exec>::read(const mutable_buffer &buf, Token &&token)
	noexcept requires task_token_v<Token,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto &error = adapted_error.get();
		error.clear();
		return read_some(buf, error);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto size = read_some(buf, error);
		return error ? io_expected(io_unexpected(error)) : io_expected(size);
	}
	else
	{
		return detail::initiate_connection_io(get_executor(),
		[this, buf](auto handler) mutable {
			co_read_some(buf, io_handler_t(std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_connection<Exec>::write(const const_buffer &body, Token &&token) noexcept
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto &error = adapted_error.get();
		error.clear();
		return write_all(body, error);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto size = write_all(body, error);
		return error ? io_expected(io_unexpected(error)) : io_expected(size);
	}
	else
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owner = detail::copy_buffer(body);
			return detail::initiate_connection_io<true>(get_executor(),
			[this, owner](auto handler) mutable
			{
				co_write_all (
					const_buffer(*owner), io_handler_t(std::move(handler))
				);
			},
			std::forward<Token>(token), owner);
		}
		else
		{
			return detail::initiate_connection_io(get_executor(),
			[this, body](auto handler) mutable {
				co_write_all(body, io_handler_t(std::move(handler)));
			},
			std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_connection<Exec>::write(std::span<const const_buffer> buffers, Token &&token) noexcept
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		auto &error = adapted_error.get();
		error.clear();
		return write_all(buffers, error);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto size = write_all(buffers, error);
		return error ? io_expected(io_unexpected(error)) : io_expected(size);
	}
	else
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owner = detail::copy_buffers(buffers);
			return detail::initiate_connection_io<true>(get_executor(),
			[this, owner](auto handler) mutable
			{
				co_write_all (
					const_buffer(*owner), io_handler_t(std::move(handler))
				);
			},
			std::forward<Token>(token), owner);
		}
		else
		{
			detail::const_buffer_sequence sequence(buffers);
			return detail::initiate_connection_io(get_executor(),
			[this, buffer_sequence = std::move(sequence)](auto handler) mutable
			{
				co_write_all (
					buffer_sequence.buffers(), io_handler_t(std::move(handler))
				);
			},
			std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
size_t basic_connection<Exec>::write_all
(std::span<const const_buffer> buffers, error_code &error) noexcept
{
	error.clear();
	size_t sum = 0;
	for( const auto &buffer : buffers )
	{
		auto size = write_all(buffer, error);
		if( size > std::numeric_limits<size_t>::max() - sum )
		{
			error = make_system_error_code(std::errc::value_too_large);
			return sum;
		}
		sum += size;
		if( error )
			return sum;
	}
	error.clear();
	return sum;
}

template <core_concepts::exec Exec>
void basic_connection<Exec>::co_write_all
(std::span<const const_buffer> buffers, io_handler_t handler) noexcept
{
	auto exec = get_executor();
	auto slot = asio::get_associated_cancellation_slot(handler);

	auto completion_exec = asio::get_associated_executor(handler, exec);
	detail::const_buffer_sequence sequence(buffers);

	asio::co_spawn(exec, [this, buffer_sequence = std::move(sequence)]
	() mutable -> awaitable<detail::connection_io_completion>
	{
		size_t sum = 0;
		for( const auto &buffer : buffer_sequence.buffers() )
		{
			error_code error {};
			auto size = co_await detail::initiate_connection_io (
			[this, buffer](auto next_handler) mutable
			{
				co_write_all (
					buffer, io_handler_t(std::move(next_handler))
				);
			},
			asio::redirect_error(use_awaitable, error));

			if( size > std::numeric_limits<size_t>::max() - sum )
			{
				co_return detail::connection_io_completion {
					make_system_error_code(std::errc::value_too_large), sum
				};
			}
			sum += size;
			if( error )
			{
				co_return detail::connection_io_completion {
					riwo::detail::canonical_error(error), sum
				};
			}
		}
		co_return detail::connection_io_completion {{}, sum};
	},
	asio::bind_executor(completion_exec,
		asio::bind_cancellation_slot(slot, [completion_handler = std::move(handler)]
		(const std::exception_ptr &exception, detail::connection_io_completion result) mutable
		{
			if( exception )
			{
				try {
					std::rethrow_exception(exception);
				}
				catch(const std::system_error &ex) {
					std::move(completion_handler)(ex.code(), 0);
				}
				catch(const std::bad_alloc&)
				{
					std::move(completion_handler) (
						make_system_error_code(std::errc::not_enough_memory), 0
					);
				}
				catch(...)
				{
					std::move(completion_handler) (
						make_system_error_code(std::errc::io_error), 0
					);
				}
				return ;
			}
			std::move(completion_handler)(result.error, result.transferred);
		})
	));
}

} //namespace riwo::http


#endif //RIWO_HTTP_UTILS_DETAIL_CONNECTION_H
