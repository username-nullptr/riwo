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

	server_exchange_state(std::shared_ptr<stream> stream = {},
		request_head request = {});
	~server_exchange_state();
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;
	[[nodiscard]] const request_head &request() const noexcept;
	[[nodiscard]] std::shared_ptr<body_reader_state> request_body() const noexcept;
	[[nodiscard]] std::shared_ptr<body_writer_state> response_body() const noexcept;
	void respond(response_head head, body_spec spec, error_code &error) noexcept;
	void respond(response_head head, body_spec spec, operation_handler handler);
	void inform(response_head head, error_code &error) noexcept;
	void inform(response_head head, operation_handler handler);
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> impl_;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream = {}, request_head request = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_server_exchange(std::forward<Exec0>(executor), {}, {})
{
}

template <core_concepts::exec Exec>
basic_server_exchange<Exec>::~basic_server_exchange() {
	if( state_ ) state_->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream, request_type request_value)
	requires core_concepts::match_sched<Exec0,executor_type> :
	executor_(get_executor_helper(std::forward<Exec0>(executor))),
	state_(detail::make_server_exchange_state(
		std::move(stream), std::move(request_value))),
	request_body_(new body_reader_type(executor_, state_->request_body())),
	response_body_(new body_writer_type(executor_, state_->response_body()))
{
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::get_executor() const noexcept -> executor_type {
	return executor_;
}

template <core_concepts::exec Exec>
protocol basic_server_exchange<Exec>::selected_protocol() const noexcept {
	return state_->selected_protocol();
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request() const noexcept -> const request_type& {
	return state_->request();
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request_body() noexcept -> body_reader_type& {
	return *request_body_;
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::response_body() noexcept -> body_writer_type& {
	return *response_body_;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(response_head head, body_spec spec,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		state_->respond(std::move(head), std::move(spec), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		state_->respond(std::move(head), std::move(spec), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = state_;
		return initiate_io_void(executor_,
		[state = std::move(state), head = std::move(head), spec = std::move(spec)]
		(auto handler) mutable {
			state->respond(std::move(head), std::move(spec),
				typename detail::server_exchange_state::operation_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Body, typename Token>
auto basic_server_exchange<Exec>::respond(response_head head, Body &&body,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code> &&
	requires(Body &&value) { std::data(value); std::size(value); }
{
	using value_type = std::remove_cv_t<std::remove_reference_t<
		decltype(*std::data(body))>>;
	static_assert(std::is_trivially_copyable_v<value_type>);
	const auto values = std::span(std::data(body), std::size(body));
	const auto bytes = std::as_bytes(values);

	if constexpr( is_error_code_token_v<Token> )
	{
		respond(std::move(head), body_spec::known(bytes.size()), token);
		if( token ) return;
		(void)response_body().write(const_buffer(bytes.data(), bytes.size()), token);
		if( token ) return;
		response_body().finish({}, token);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		respond(std::move(head), body_spec::known(bytes.size()));
		(void)response_body().write(const_buffer(bytes.data(), bytes.size()));
		response_body().finish();
	}
	else
	{
		auto owned = std::make_shared<std::vector<std::byte>>(
			bytes.begin(), bytes.end());
		return initiate_io_void(executor_,
		[this, head = std::move(head), owned](auto handler) mutable
		{
			respond(std::move(head), body_spec::known(owned->size()),
			[this, owned, handler = std::move(handler)](error_code error) mutable
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
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::inform(response_head head, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		state_->inform(std::move(head), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		state_->inform(std::move(head), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = state_;
		return initiate_io_void(executor_,
		[state = std::move(state), head = std::move(head)]
		(auto handler) mutable {
			state->inform(std::move(head),
				typename detail::server_exchange_state::operation_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_server_exchange<Exec>::cancel() noexcept {
	state_->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_server_exchange<Exec>::capabilities() const noexcept {
	return state_->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_server_exchange<Exec>::extension() noexcept {
	return nullptr;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H
