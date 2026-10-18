// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_READER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_READER_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API body_reader_adapter
{
	RIWO_DISABLE_COPY_MOVE(body_reader_adapter)

public:
	using read_handler = asio::any_completion_handler<void(error_code,size_t)>;
	using trailers_handler = asio::any_completion_handler<void(error_code,fields)>;

	body_reader_adapter() = default;
	virtual ~body_reader_adapter();

	[[nodiscard]] virtual size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept = 0;
	virtual void read_some(mutable_buffer buffer, read_handler handler) = 0;
	[[nodiscard]] virtual fields trailers_sync(error_code &error) noexcept = 0;
	virtual void trailers(trailers_handler handler) = 0;
	virtual void cancel() noexcept = 0;
};

class RIWO_HTTP_EXP_API body_reader_state final :
	public std::enable_shared_from_this<body_reader_state>
{
	RIWO_DISABLE_COPY_MOVE(body_reader_state)

public:
	using read_handler = body_reader_adapter::read_handler;
	using operation_handler = asio::any_completion_handler<void(error_code)>;
	using trailers_handler = body_reader_adapter::trailers_handler;

	explicit body_reader_state(std::shared_ptr<body_reader_adapter> adapter = {},
		bool ready = false, uint64_t max_discard = 1024 * 1024);
	~body_reader_state();

	[[nodiscard]] size_t read_some(mutable_buffer buffer,
		error_code &error) noexcept;
	void read_some(mutable_buffer buffer, read_handler handler);
	void discard(error_code &error) noexcept;
	void discard(operation_handler handler);
	[[nodiscard]] fields trailers(error_code &error) noexcept;
	void trailers(trailers_handler handler);
	void cancel() noexcept;
	void set_ready(bool ready = true) noexcept;
	[[nodiscard]] bool ended() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> impl_;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<body_reader_state> make_body_reader_state(
	std::shared_ptr<body_reader_adapter> adapter = {}, bool ready = false,
	uint64_t max_discard = 1024 * 1024);

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_reader<Exec>::basic_body_reader(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_body_reader(std::forward<Exec0>(executor),
		detail::make_body_reader_state())
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_reader<Exec>::basic_body_reader(Exec0 &&executor,
	std::shared_ptr<detail::body_reader_state> state)
	requires core_concepts::match_sched<Exec0,executor_type> :
	executor_(get_executor_helper(std::forward<Exec0>(executor))),
	state_(state ? std::move(state) : detail::make_body_reader_state())
{
}

template <core_concepts::exec Exec>
basic_body_reader<Exec>::~basic_body_reader() {
	if( state_ && !state_->ended() ) state_->cancel();
}

template <core_concepts::exec Exec>
auto basic_body_reader<Exec>::get_executor() const noexcept -> executor_type {
	return executor_;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_body_reader<Exec>::read_some(mutable_buffer buffer, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
		return state_->read_some(buffer, token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto size = state_->read_some(buffer, error);
		return expected_value_or_throw(error ?
			sys_expected<size_t>(sys_unexpected(error)) : sys_expected<size_t>(size));
	}
	else
	{
		auto state = state_;
		return initiate_io<size_t>(executor_,
		[state = std::move(state), buffer](auto handler) mutable {
			state->read_some(buffer,
				typename detail::body_reader_state::read_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_body_reader<Exec>::discard(Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		state_->discard(token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		state_->discard(error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = state_;
		return initiate_io_void(executor_,
		[state = std::move(state)](auto handler) mutable {
			state->discard(typename detail::body_reader_state::operation_handler(
				std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_body_reader<Exec>::trailers(Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,fields>
{
	if constexpr( is_error_code_token_v<Token> )
		return state_->trailers(token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto value = state_->trailers(error);
		return expected_value_or_throw(error ?
			sys_expected<fields>(sys_unexpected(error)) :
			sys_expected<fields>(std::move(value)));
	}
	else
	{
		auto state = state_;
		return initiate_io<fields>(executor_,
		[state = std::move(state)](auto handler) mutable {
			state->trailers(typename detail::body_reader_state::trailers_handler(
				std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_body_reader<Exec>::cancel() noexcept {
	state_->cancel();
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_DETAIL_READER_H
