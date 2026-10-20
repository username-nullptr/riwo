// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_EXCHANGE_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_EXCHANGE_H

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API client_exchange_state
{
	RIWO_DISABLE_COPY_MOVE(client_exchange_state)

public:
	using response_handler = asio::any_completion_handler<void(
		error_code,response_head)>;
	using duplex_handler = asio::any_completion_handler<void(
		error_code,std::shared_ptr<duplex_state>)>;

	explicit client_exchange_state(std::shared_ptr<stream> stream = {});
	~client_exchange_state();
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;
	[[nodiscard]] std::shared_ptr<body_writer_state> request_body() const noexcept;
	[[nodiscard]] std::shared_ptr<body_reader_state> response_body() const noexcept;
	[[nodiscard]] response_head wait_response(error_code &error) noexcept;
	void wait_response(response_handler handler);
	[[nodiscard]] std::shared_ptr<duplex_state> release_duplex(
		error_code &error) noexcept;
	void release_duplex(duplex_handler handler);
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<client_exchange_state> make_client_exchange_state(
	std::shared_ptr<stream> stream = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_client_exchange<Exec>::impl
{
public:
	impl(executor_type executor_value, std::shared_ptr<detail::stream> stream) :
		m_executor(std::move(executor_value)),
		m_state(detail::make_client_exchange_state(std::move(stream))),
		m_request_body(std::make_unique<body_writer_type>(
			m_executor, m_state->request_body())),
		m_response_body(std::make_unique<body_reader_type>(
			m_executor, m_state->response_body()))
	{
	}

	executor_type m_executor;
	std::shared_ptr<detail::client_exchange_state> m_state;
	std::unique_ptr<body_writer_type> m_request_body;
	std::unique_ptr<body_reader_type> m_response_body;
};

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::basic_client_exchange()
	requires std::default_initializable<executor_type> :
	basic_client_exchange(executor_type{})
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client_exchange<Exec>::basic_client_exchange(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_client_exchange(std::forward<Exec0>(executor), {})
{
}

template <core_concepts::exec Exec>
basic_client_exchange<Exec>::~basic_client_exchange() {
	if( m_impl and m_impl->m_state ) m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client_exchange<Exec>::basic_client_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream)
	requires core_concepts::match_sched<Exec0,executor_type> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)), std::move(stream)))
{
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->m_executor;
}

template <core_concepts::exec Exec>
protocol basic_client_exchange<Exec>::selected_protocol() const noexcept {
	return m_impl->m_state->selected_protocol();
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::request_body() noexcept -> body_writer_type& {
	return *m_impl->m_request_body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client_exchange<Exec>::wait_response(Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response_head>
{
	if constexpr( is_error_code_token_v<Token> )
		return m_impl->m_state->wait_response(token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto head = m_impl->state->wait_response(error);
		return expected_value_or_throw(error ?
			sys_expected<response_head>(sys_unexpected(error)) :
			sys_expected<response_head>(std::move(head)));
	}
	else
	{
		auto state = m_impl->m_state;
		return initiate_io<response_head>(m_impl->m_executor,
		[state = std::move(state)](auto handler) mutable {
			state->wait_response(
				typename detail::client_exchange_state::response_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::response_body() noexcept -> body_reader_type& {
	return *m_impl->m_response_body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client_exchange<Exec>::release_duplex(Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_duplex_channel<executor_type>>
{
	using channel_type = basic_duplex_channel<executor_type>;
	if constexpr( is_error_code_token_v<Token> )
	{
		auto state = m_impl->state->release_duplex(token);
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
		auto state = m_impl->state->release_duplex(error);
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
		auto executor = m_impl->m_executor;
		return initiate_io<channel_type>(executor,
		[owner = std::move(owner), executor](auto handler) mutable {
			owner->m_state->release_duplex(
				[owner = std::move(owner), executor,
				 handler = std::move(handler)]
				(error_code error,
				 std::shared_ptr<detail::duplex_state> channel) mutable {
					if( not error )
					{
						owner->m_request_body->detach();
						owner->m_response_body->detach();
					}
					std::move(handler)(error,
						channel_type(executor, std::move(channel)));
				});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_client_exchange<Exec>::cancel() noexcept {
	m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_client_exchange<Exec>::capabilities() const noexcept {
	return m_impl->state->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_client_exchange<Exec>::extension() noexcept {
	return nullptr;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_EXCHANGE_H
