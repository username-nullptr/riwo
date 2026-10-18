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

	explicit client_exchange_state(std::shared_ptr<stream> stream = {});
	~client_exchange_state();
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;
	[[nodiscard]] std::shared_ptr<body_writer_state> request_body() const noexcept;
	[[nodiscard]] std::shared_ptr<body_reader_state> response_body() const noexcept;
	[[nodiscard]] response_head wait_response(error_code &error) noexcept;
	void wait_response(response_handler handler);
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> impl_;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<client_exchange_state> make_client_exchange_state(
	std::shared_ptr<stream> stream = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

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
	if( state_ ) state_->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client_exchange<Exec>::basic_client_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream)
	requires core_concepts::match_sched<Exec0,executor_type> :
	executor_(get_executor_helper(std::forward<Exec0>(executor))),
	state_(detail::make_client_exchange_state(std::move(stream))),
	request_body_(new body_writer_type(executor_, state_->request_body())),
	response_body_(new body_reader_type(executor_, state_->response_body()))
{
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::get_executor() const noexcept -> executor_type {
	return executor_;
}

template <core_concepts::exec Exec>
protocol basic_client_exchange<Exec>::selected_protocol() const noexcept {
	return state_->selected_protocol();
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::request_body() noexcept -> body_writer_type& {
	return *request_body_;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client_exchange<Exec>::wait_response(Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response_head>
{
	if constexpr( is_error_code_token_v<Token> )
		return state_->wait_response(token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto head = state_->wait_response(error);
		return expected_value_or_throw(error ?
			sys_expected<response_head>(sys_unexpected(error)) :
			sys_expected<response_head>(std::move(head)));
	}
	else
	{
		auto state = state_;
		return initiate_io<response_head>(executor_,
		[state = std::move(state)](auto handler) mutable {
			state->wait_response(
				typename detail::client_exchange_state::response_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
auto basic_client_exchange<Exec>::response_body() noexcept -> body_reader_type& {
	return *response_body_;
}

template <core_concepts::exec Exec>
void basic_client_exchange<Exec>::cancel() noexcept {
	state_->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_client_exchange<Exec>::capabilities() const noexcept {
	return state_->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_client_exchange<Exec>::extension() noexcept {
	return nullptr;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_EXCHANGE_H
