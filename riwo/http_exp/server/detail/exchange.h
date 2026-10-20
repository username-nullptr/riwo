// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API server_exchange_state
{
	RIWO_DISABLE_COPY_MOVE(server_exchange_state)

public:
	using operation_handler = asio::any_completion_handler<void(error_code)>;
	using duplex_handler = asio::any_completion_handler<void(
		error_code,std::shared_ptr<duplex_state>)>;

	server_exchange_state(std::shared_ptr<stream> stream = {},
		request_head request = {});
	~server_exchange_state();
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;
	[[nodiscard]] const request_head &request() const noexcept;
	[[nodiscard]] std::shared_ptr<body_reader_state> request_body() const noexcept;
	[[nodiscard]] std::shared_ptr<body_writer_state> response_body() const noexcept;
	void respond(response_head head, body_spec spec, error_code &error) noexcept;
	void respond(response_head head, body_spec spec, operation_handler handler);
	void inform(response_head head, error_code &error) noexcept;
	void inform(response_head head, operation_handler handler);
	[[nodiscard]] std::shared_ptr<duplex_state> accept_duplex(
		response_head head, error_code &error) noexcept;
	void accept_duplex(response_head head, duplex_handler handler);
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream = {}, request_head request = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_server_exchange<Exec>::impl
{
public:
	impl(executor_type executor_value, std::shared_ptr<detail::stream> stream,
		request_type request) : executor(std::move(executor_value)),
		state(detail::make_server_exchange_state(
			std::move(stream), std::move(request))),
		request_body(std::make_unique<body_reader_type>(
			executor, state->request_body())),
		response_body(std::make_unique<body_writer_type>(
			executor, state->response_body()))
	{
	}

	executor_type executor;
	std::shared_ptr<detail::server_exchange_state> state;
	std::unique_ptr<body_reader_type> request_body;
	std::unique_ptr<body_writer_type> response_body;
};

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_server_exchange(std::forward<Exec0>(executor), {}, {})
{
}

template <core_concepts::exec Exec>
basic_server_exchange<Exec>::~basic_server_exchange() {
	if( m_impl->state ) m_impl->state->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream, request_type request_value)
	requires core_concepts::match_sched<Exec0,executor_type> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)),
		std::move(stream), std::move(request_value)))
{
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->executor;
}

template <core_concepts::exec Exec>
protocol basic_server_exchange<Exec>::selected_protocol() const noexcept {
	return m_impl->state->selected_protocol();
}

template <core_concepts::exec Exec>
std::string basic_server_exchange<Exec>::remote_address() const {
	return m_impl->state->remote_address();
}

template <core_concepts::exec Exec>
uint16_t basic_server_exchange<Exec>::remote_port() const noexcept {
	return m_impl->state->remote_port();
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request() const noexcept -> const request_type& {
	return m_impl->state->request();
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request_body() noexcept -> body_reader_type& {
	return *m_impl->request_body;
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::response_body() noexcept -> body_writer_type& {
	return *m_impl->response_body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(response_head head, body_spec spec,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		m_impl->state->respond(std::move(head), std::move(spec), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->state->respond(std::move(head), std::move(spec), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = m_impl->state;
		return initiate_io_void(m_impl->executor,
		[state = std::move(state), head = std::move(head), spec = std::move(spec)]
		(auto handler) mutable {
			state->respond(std::move(head), std::move(spec),
				typename detail::server_exchange_state::operation_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(response_head head,
	const const_buffer &body, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		respond(std::move(head), body_spec::known(body.size()), token);
		if( token ) return;
		(void)response_body().write(body, token);
		if( token ) return;
		response_body().finish({}, token);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		respond(std::move(head), body_spec::known(body.size()));
		(void)response_body().write(body);
		response_body().finish();
	}
	else
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owned = std::make_shared<std::vector<std::byte>>(body.size());
			if( body.size() != 0 )
				std::memcpy(owned->data(), body.data(), body.size());
			return initiate_io_void(m_impl->executor,
			[this, head = std::move(head), owned](auto handler) mutable
			{
				respond(std::move(head), body_spec::known(owned->size()),
				[this, owned, handler = std::move(handler)]
				(error_code error) mutable
				{
					if( error )
					{
						std::move(handler)(error);
						return;
					}
					response_body().write(riwo::buffer(*owned),
					[this, owned, handler = std::move(handler)]
					(error_code write_error, size_t) mutable
					{
						if( write_error )
						{
							std::move(handler)(write_error);
							return;
						}
						response_body().finish({}, std::move(handler));
					});
				});
			}, std::forward<Token>(token));
		}
		else
		{
			return initiate_io_void(m_impl->executor,
			[this, head = std::move(head), body](auto handler) mutable
			{
				respond(std::move(head), body_spec::known(body.size()),
				[this, body, handler = std::move(handler)]
				(error_code error) mutable
				{
					if( error )
					{
						std::move(handler)(error);
						return;
					}
					response_body().write(body,
					[this, handler = std::move(handler)]
					(error_code write_error, size_t) mutable
					{
						if( write_error )
						{
							std::move(handler)(write_error);
							return;
						}
						response_body().finish({}, std::move(handler));
					});
				});
			}, std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::inform(response_head head, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		m_impl->state->inform(std::move(head), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->state->inform(std::move(head), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = m_impl->state;
		return initiate_io_void(m_impl->executor,
		[state = std::move(state), head = std::move(head)]
		(auto handler) mutable {
			state->inform(std::move(head),
				typename detail::server_exchange_state::operation_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::accept_duplex(response_head head,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_duplex_channel<executor_type>>
{
	using channel_type = basic_duplex_channel<executor_type>;
	if constexpr( is_error_code_token_v<Token> )
	{
		auto state = m_impl->state->accept_duplex(std::move(head), token);
		if( not token )
		{
			m_impl->request_body->detach();
			m_impl->response_body->detach();
		}
		return channel_type(m_impl->executor, std::move(state));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto state = m_impl->state->accept_duplex(std::move(head), error);
		if( error )
			return expected_value_or_throw(sys_expected<channel_type>(
				sys_unexpected(error)));
		m_impl->request_body->detach();
		m_impl->response_body->detach();
		return channel_type(m_impl->executor, std::move(state));
	}
	else
	{
		auto owner = m_impl;
		auto executor = m_impl->executor;
		return initiate_io<channel_type>(executor,
		[owner = std::move(owner), executor,
		 head = std::move(head)](auto handler) mutable {
			owner->state->accept_duplex(std::move(head),
				[owner = std::move(owner), executor,
				 handler = std::move(handler)]
				(error_code error,
				 std::shared_ptr<detail::duplex_state> channel) mutable {
					if( not error )
					{
						owner->request_body->detach();
						owner->response_body->detach();
					}
					std::move(handler)(error,
						channel_type(executor, std::move(channel)));
				});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_server_exchange<Exec>::cancel() noexcept {
	m_impl->state->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_server_exchange<Exec>::capabilities() const noexcept {
	return m_impl->state->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_server_exchange<Exec>::extension() noexcept {
	return nullptr;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H
