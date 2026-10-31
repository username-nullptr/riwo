// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_DETAIL_EXCHANGE_H
#define RIWO_EXP_HTTP_CLIENT_DETAIL_EXCHANGE_H

#include <riwo/experimental/http/session/session_stream.h>

namespace riwo::experimental::http { namespace detail
{

class RIWO_EXP_HTTP_API client_exchange_state
{
	RIWO_DISABLE_COPY_MOVE(client_exchange_state)

public:
	using reply_handler_t = asio::any_completion_handler <
		void(error_code, reply_head)
	>;
	using duplex_handler_t = asio::any_completion_handler <
		void(error_code, duplex_state_ptr)
	>;
	explicit client_exchange_state(session_stream_ptr stream = {},
		cookie_jar_ptr cookies = {}, url origin = {}
	);
	~client_exchange_state();

public:
	[[nodiscard]] reply_head wait_reply(error_code &error) noexcept;
	void wait_reply(reply_handler_t handler);

	[[nodiscard]] duplex_state_ptr release_duplex(error_code &error) noexcept;
	void release_duplex(duplex_handler_t handler);

	void cancel() noexcept;

public:
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	[[nodiscard]] body_writer_state_ptr request_body() const noexcept;
	[[nodiscard]] body_reader_state_ptr reply_body() const noexcept;

	[[nodiscard]] void *protocol_extension(const std::type_info &type) noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using client_exchange_state_ptr = std::shared_ptr<client_exchange_state>;

[[nodiscard]] RIWO_EXP_HTTP_API
client_exchange_state_ptr make_client_exchange_state (
	session_stream_ptr stream = {}, cookie_jar_ptr cookies = {}, url origin = {}
);

} //namespace detail

template <core_concepts::exec Exec>
class RIWO_EXP_HTTP_TAPI basic_client_exchange<Exec>::impl
{
public:
	impl(executor_t executor_value, session_stream_ptr stream, cookie_jar_ptr cookies, url origin) :
		m_exec(std::move(executor_value)),
		m_state(detail::make_client_exchange_state(std::move(stream),
			std::move(cookies), std::move(origin))
		),
		m_request_body(m_exec, m_state->request_body()),
		m_reply_body(m_exec, m_state->reply_body()) {}

	executor_t m_exec {};
	detail::client_exchange_state_ptr m_state {};

	body_writer_t m_request_body {};
	body_reader_t m_reply_body {};
};

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::basic_client_exchange(basic_client_exchange &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_shared<impl>(m_impl->m_exec,
		session_stream_ptr{}, cookie_jar_ptr{}, url{}
	);
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::operator=(basic_client_exchange &&other)
	noexcept -> basic_client_exchange&
{
	if( this == &other )
		return *this;

	auto replacement = std::make_shared<impl>(other.m_impl->m_exec,
		session_stream_ptr{}, cookie_jar_ptr{}, url{}
	);
	if( m_impl and m_impl->m_state )
		m_impl->m_state->cancel();

	m_impl = std::move(other.m_impl);
	other.m_impl = std::move(replacement);
	return *this;
}

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::basic_client_exchange() requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_client_exchange(executor_t(riwo::get_executor()))
{
}

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::basic_client_exchange
(session_stream_ptr stream, cookie_jar_ptr cookies, url origin) requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_client_exchange(executor_t(riwo::get_executor()),
		std::move(stream), std::move(cookies), std::move(origin)
	)
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client_exchange<Exec>::basic_client_exchange(Exec0 &&executor) requires
	core_concepts::match_sched<Exec0,executor_t> :
	basic_client_exchange(std::forward<Exec0>(executor), {})
{

}

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::~basic_client_exchange()
{
	if( m_impl and m_impl->m_state )
		m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client_exchange<Exec>::basic_client_exchange
(Exec0 &&executor, session_stream_ptr stream, cookie_jar_ptr cookies, url origin) requires
	core_concepts::match_sched<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)), std::move(stream),
		std::move(cookies), std::move(origin)
	))
{

}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::get_executor() const noexcept -> executor_t
{
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
protocol basic_client_exchange<Exec>::selected_protocol() const noexcept
{
	return m_impl->m_state->selected_protocol();
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::request_body() noexcept -> body_writer_t&
{
	return m_impl->m_request_body;
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,reply_head> Token>
auto basic_client_exchange<Exec>::wait_reply(Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		return m_impl->m_state->wait_reply(token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto head = m_impl->m_state->wait_reply(error);

		return expected_value_or_throw(error ?
			sys_expected<reply_head>(sys_unexpected(error)) :
			sys_expected<reply_head>(std::move(head))
		);
	}
	else
	{
		auto state = m_impl->m_state;
		return initiate_io<reply_head>(m_impl->m_exec,
		[state = std::move(state)](auto handler) mutable
		{
			state->wait_reply (
				detail::client_exchange_state::reply_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::reply_body() noexcept -> body_reader_t&
{
	return m_impl->m_reply_body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client_exchange<Exec>::release_duplex(Token &&token) requires
	core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_duplex_channel<executor_t>>
{
	using channel_t = basic_duplex_channel<executor_t>;
	if constexpr( is_error_code_token_v<Token> )
	{
		auto state = m_impl->m_state->release_duplex(token);
		if( not token )
		{
			m_impl->m_request_body.detach();
			m_impl->m_reply_body.detach();
		}
		return channel_t(m_impl->m_exec, std::move(state));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto state = m_impl->m_state->release_duplex(error);

		if( error )
			return expected_value_or_throw(sys_expected<channel_t>(sys_unexpected(error)));

		m_impl->m_request_body.detach();
		m_impl->m_reply_body.detach();

		return channel_t(m_impl->m_exec, std::move(state));
	}
	else
	{
		auto owner = m_impl;
		auto executor = m_impl->m_exec;

		return initiate_io<channel_t>(executor,
		[owner = std::move(owner), executor](auto handler) mutable
		{
			owner->m_state->release_duplex (
			[owner = std::move(owner), executor, handler = std::move(handler)]
			(error_code error, detail::duplex_state_ptr channel) mutable
			{
				if( not error )
				{
					owner->m_request_body.detach();
					owner->m_reply_body.detach();
				}
				std::move(handler) (
					error, channel_t(executor, std::move(channel))
				);
			});
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_client_exchange<Exec>::cancel() noexcept
{
	m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_client_exchange<Exec>::capabilities() const noexcept
{
	return m_impl->m_state->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_client_exchange<Exec>::extension() noexcept
{
	return static_cast<Extension*>(
		m_impl->m_state->protocol_extension(typeid(Extension))
	);
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_CLIENT_DETAIL_EXCHANGE_H
