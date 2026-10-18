// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H

#include <riwo/http_exp/body/detail/unsupported.h>

namespace riwo::http_exp::detail
{

class stream;

struct send_result
{
	protocol selected_protocol = protocol::v1_1;
	response_head head {};
	std::shared_ptr<body_reader_state> body;
	std::shared_ptr<void> owner;
};

class RIWO_HTTP_EXP_API client_state
{
	RIWO_DISABLE_COPY_MOVE(client_state)

public:
	using open_handler = asio::any_completion_handler<void(
		error_code,std::shared_ptr<stream>)>;
	using send_handler = asio::any_completion_handler<void(
		error_code,send_result)>;

	explicit client_state(std::shared_ptr<client_session> session = {});
	~client_state();
	[[nodiscard]] std::shared_ptr<stream> open(request request_value,
		body_spec spec, error_code &error) noexcept;
	void open(request request_value, body_spec spec, open_handler handler);
	[[nodiscard]] send_result send(request request_value,
		error_code &error) noexcept;
	void send(request request_value, send_handler handler);

private:
	std::shared_ptr<client_session> session_;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<client_state> make_client_state(
	std::shared_ptr<client_session> session = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor, config_type config)
	requires (
		!std::same_as<std::remove_cvref_t<Exec0>,basic_client> &&
		core_concepts::match_sched<Exec0,executor_type>) :
	basic_client(std::forward<Exec0>(executor), {}, std::move(config))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor,
	std::shared_ptr<detail::client_session> session, config_type config)
	requires (
		!std::same_as<std::remove_cvref_t<Exec0>,basic_client> &&
		core_concepts::match_sched<Exec0,executor_type>) :
	executor_(get_executor_helper(std::forward<Exec0>(executor))),
	config_(std::move(config)),
	state_(detail::make_client_state(std::move(session)))
{
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::get_executor() const noexcept -> executor_type {
	return executor_;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::config() const noexcept -> const config_type& {
	return config_;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::send(request request_value, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_type>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = state_->send(std::move(request_value), token);
		return token ? reply_type(executor_) : reply_type(executor_,
			result.selected_protocol, std::move(result.head), std::move(result.body),
			{}, std::move(result.owner));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = state_->send(std::move(request_value), error);
		if( error )
			return expected_value_or_throw(sys_expected<reply_type>(
				sys_unexpected(error)));
		return reply_type(executor_, result.selected_protocol,
			std::move(result.head), std::move(result.body), {},
			std::move(result.owner));
	}
	else
	{
		auto state = state_;
		auto executor = executor_;
		return initiate_io<reply_type>(executor_,
		[state = std::move(state), executor,
		 request_value = std::move(request_value)](auto handler) mutable
		{
			state->send(std::move(request_value),
			[handler = std::move(handler), executor]
			(error_code error, detail::send_result result) mutable
			{
				if( error )
				{
					std::move(handler)(error, reply_type(executor));
					return;
				}
				std::move(handler)(error_code{}, reply_type(executor,
					result.selected_protocol, std::move(result.head),
					std::move(result.body), {}, std::move(result.owner)));
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::fetch(request request_value, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_type>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = state_->send(std::move(request_value), token);
		if( token ) return reply_type(executor_);
		reply_type active(executor_, result.selected_protocol,
			result.head, result.body, {}, result.owner);
		auto bytes = active.read_all(token);
		if( token ) return reply_type(executor_);
		return reply_type(executor_, result.selected_protocol,
			std::move(result.head), std::move(result.body), std::move(bytes),
			std::move(result.owner));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = state_->send(std::move(request_value), error);
		if( error )
			return expected_value_or_throw(sys_expected<reply_type>(
				sys_unexpected(error)));
		reply_type active(executor_, result.selected_protocol,
			result.head, result.body, {}, result.owner);
		auto bytes = active.read_all();
		return reply_type(executor_, result.selected_protocol,
			std::move(result.head), std::move(result.body), std::move(bytes),
			std::move(result.owner));
	}
	else
	{
		auto state = state_;
		auto executor = executor_;
		return initiate_io<reply_type>(executor_,
		[state = std::move(state), executor,
		 request_value = std::move(request_value)](auto handler) mutable
		{
			state->send(std::move(request_value),
			[handler = std::move(handler), executor]
			(error_code error, detail::send_result result) mutable
			{
				if( error )
				{
					std::move(handler)(error, reply_type(executor));
					return;
				}
				auto active = std::make_shared<reply_type>(executor,
					result.selected_protocol, result.head, result.body,
					std::vector<std::byte>{}, result.owner);
				active->read_all(
				[handler = std::move(handler), executor, active,
				 result = std::move(result)]
				(error_code read_error, std::vector<std::byte> bytes) mutable
				{
					if( read_error )
					{
						std::move(handler)(read_error, reply_type(executor));
						return;
					}
					std::move(handler)(error_code{}, reply_type(executor,
						result.selected_protocol, std::move(result.head),
						std::move(result.body), std::move(bytes),
						std::move(result.owner)));
				});
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(request request_value, body_spec spec,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,exchange_type>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto stream = state_->open(std::move(request_value), std::move(spec), token);
		return exchange_type(executor_, std::move(stream));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto stream = state_->open(std::move(request_value), std::move(spec), error);
		if( error )
			return expected_value_or_throw(sys_expected<exchange_type>(
				sys_unexpected(error)));
		return exchange_type(executor_, std::move(stream));
	}
	else
	{
		auto state = state_;
		auto executor = executor_;
		return initiate_io<exchange_type>(executor_,
		[state = std::move(state), executor,
		 request_value = std::move(request_value), spec = std::move(spec)]
		(auto handler) mutable
		{
			state->open(std::move(request_value), std::move(spec),
			[handler = std::move(handler), executor]
			(error_code error, std::shared_ptr<detail::stream> stream) mutable
			{
				try {
					std::move(handler)(error,
						exchange_type(executor, std::move(stream)));
				}
				catch(...) {
					std::move(handler)(exception_error(
						std::current_exception()), exchange_type(executor));
				}
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Url, typename Token>
auto basic_client<Exec>::open(http_exp::method request_method, Url &&url,
	body_spec spec, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,exchange_type>
{
	return open(request(std::move(request_method),
		riwo::url(std::forward<Url>(url))), std::move(spec),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Url, typename Token>
auto basic_client<Exec>::get(Url &&url, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_type>
{
	return send(request(method::get, riwo::url(std::forward<Url>(url))),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Url, typename Body, typename Token>
auto basic_client<Exec>::post(Url &&url, Body &&body, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_type>
{
	request value(method::post, riwo::url(std::forward<Url>(url)));
	value.body(std::forward<Body>(body));
	return send(std::move(value), std::forward<Token>(token));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H
