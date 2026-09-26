// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_RETRY_H
#define RIWO_WEBSOCKET_DETAIL_RETRY_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#ifndef RIWO_WEBSOCKET_RETRY_H
# error "Include <riwo/websocket/retry.h> instead."
#endif

namespace riwo::websocket { namespace detail
{

using retry_open_request_factory = std::function <
	awaitable<connect_request>(const retry_open_context&)
>;

[[nodiscard]] RIWO_WEBSOCKET_API
bool valid_retry_open_options(const retry_open_options &options) noexcept;

RIWO_WEBSOCKET_API void observe_retry_open
(const retry_open_options &options, retry_open_event event, const retry_open_context &context) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API retry_open_decision
decide_retry_open(const retry_open_options &options, const retry_open_context &context) noexcept;

template <typename Factory>
[[nodiscard]] RIWO_WEBSOCKET_TAPI
retry_open_request_factory adapt_retry_open_request_factory(Factory &&factory)
{
	using callback_t = std::remove_cvref_t<Factory>;
	return [callback = callback_t(std::forward<Factory>(factory))]
	(const retry_open_context &context) mutable -> awaitable<connect_request>
	{
		using return_t = std::invoke_result_t<callback_t&,const retry_open_context&>;
		if constexpr( is_awaitable_v<return_t> )
			co_return co_await std::invoke(callback, context);
		else
			co_return std::invoke(callback, context);
	};
}

template <typename Exec>
[[nodiscard]] RIWO_WEBSOCKET_TAPI
asio::awaitable<optional<std::tuple<error_code,basic_retry_open_result<Exec>>>,Exec>
co_retry_open(basic_client<Exec> *active_client, retry_open_request_factory active_factory,
	retry_open_options active_options, Exec active_exec)
{
	using result_t = basic_retry_open_result<Exec>;
	using completion_t = std::tuple<error_code,result_t>;

	auto cancellation = co_await asio::this_coro::cancellation_state;
	result_t result(active_exec);

	if( not valid_retry_open_options(active_options) )
	{
		auto error = make_system_error_code(std::errc::invalid_argument);
		result.last_failure.error = error;

		co_return completion_t {
			error, std::move(result)
		};
	}
	retry_open_context previous;
	for(;;)
	{
		if( cancellation.cancelled() != asio::cancellation_type::none )
		{
			auto error = asio::error::make_error_code (
				asio::error::operation_aborted
			);
			result.last_failure = previous;
			result.last_failure.error = error;

			co_return completion_t {
				error, std::move(result)
			};
		}
		connect_request request;
		try {
			auto generated_result = co_await asio::co_spawn (
				active_exec, active_factory(previous), asio::as_tuple(deferred)
			);
			auto &[exception, generated] = generated_result;
			if( auto error = exception_error(exception) )
			{
				result.last_failure = previous;
				result.last_failure.error = error;

				co_return completion_t {
					error, std::move(result)
				};
			}
			request = std::move(generated);
		}
		catch(...)
		{
			auto error = exception_error(std::current_exception());
			result.last_failure = previous;
			result.last_failure.error = error;

			co_return completion_t {
				error, std::move(result)
			};
		}
		retry_open_context opening {
			.attempt = result.attempts + 1,
			.endpoint = request.endpoint,
		};
		observe_retry_open(active_options,
			retry_open_event::opening, opening
		);
		typename result_t::diagnostics_t diagnostics;
		++result.attempts;

		auto open_result = co_await active_client->open (
			std::move(request), diagnostics, asio::as_tuple(asio::use_awaitable_t<Exec>{})
		);
		auto &[open_error, stream] = open_result;
		if( not open_error )
		{
			result.stream = std::move(stream);
			result.diagnostics = std::move(diagnostics);

			co_return completion_t {
				error_code{}, std::move(result)
			};
		}
		retry_open_context failure {
			.attempt = result.attempts,
			.error = open_error,
			.endpoint = diagnostics.endpoint,
		};
		if( diagnostics.reply )
			failure.http_status = diagnostics.reply->status();

		result.diagnostics = std::move(diagnostics);
		result.last_failure = failure;

		if( open_error == asio::error::operation_aborted or
			(active_options.max_attempts != 0 and
			 result.attempts >= active_options.max_attempts) )
		{
			co_return completion_t {
				open_error, std::move(result)
			};
		}
		auto [retry, delay] = decide_retry_open(active_options, failure);
		if( not retry )
		{
			co_return completion_t {
				open_error, std::move(result)
			};
		}
		delay = std::max(delay, std::chrono::milliseconds::zero());
		observe_retry_open(active_options, retry_open_event::waiting, failure);

		asio::steady_timer timer(active_exec);
		timer.expires_after(delay);

		auto wait_result = co_await timer.async_wait (
			asio::as_tuple(asio::use_awaitable_t<Exec>{})
		);
		if( auto &[wait_error] = wait_result; wait_error )
		{
			auto error = riwo::detail::canonical_error(wait_error);
			result.last_failure.error = error;

			co_return completion_t {
				error, std::move(result)
			};
		}
		previous = std::move(failure);
	}
}

template <typename Exec, typename Token>
[[nodiscard]] RIWO_WEBSOCKET_TAPI auto initiate_retry_open(basic_client<Exec> &client,
	retry_open_request_factory request_factory, retry_open_options options, Token &&token)
{
	using result_t = basic_retry_open_result<Exec>;
	using token_t = std::remove_cvref_t<Token>;

	token_t completion_token(std::forward<Token>(token));
	auto operation_exec = client.get_executor();

	return asio::async_initiate<token_t,void(error_code,result_t)>([
		operation_exec, active_client = &client, active_factory = std::move(request_factory),
		active_options = std::move(options)
	](auto completion_handler) mutable
	{
		using handler_t = decltype(completion_handler);
		auto error_result = [operation_exec]() mutable {
			return result_t(operation_exec);
		};
		using factory_t = decltype(error_result);

		riwo::detail::launch_awaitable(operation_exec,
			co_retry_open(active_client, std::move(active_factory),
				std::move(active_options), operation_exec
			),
			riwo::detail::awaitable_optional_tuple_io_handler
			<result_t,handler_t,decltype(operation_exec),factory_t>(
				std::move(completion_handler), operation_exec,
				std::move(error_result)
			)
		);
	},
	completion_token);
}

} //namespace detail

template <typename Exec, typename Token>
auto retry_open
(basic_client<Exec> &client, connect_request request, retry_open_options options, Token &&token)
	requires concepts::retry_open_token<Exec,Token>
{
	auto factory = detail::adapt_retry_open_request_factory (
	[request = std::move(request)](const retry_open_context&) mutable {
		return request;
	});
	return detail::initiate_retry_open(client, std::move(factory),
		std::move(options), std::forward<Token>(token)
	);
}

template <typename Exec, typename Token>
auto retry_open(basic_client<Exec> &client, connect_request request, Token &&token)
	requires concepts::retry_open_token<Exec,Token>
{
	return retry_open(client, std::move(request), retry_open_options{},
		std::forward<Token>(token)
	);
}

template <typename Exec, typename Factory, typename Token>
auto retry_open
(basic_client<Exec> &client, Factory &&factory, retry_open_options options, Token &&token) requires
	concepts::retry_open_request_factory<Factory> and concepts::retry_open_token<Exec,Token> and
	(not std::same_as<std::remove_cvref_t<Factory>,connect_request>)
{
	return detail::initiate_retry_open(client,
		detail::adapt_retry_open_request_factory(std::forward<Factory>(factory)),
		std::move(options), std::forward<Token>(token)
	);
}

template <typename Exec, typename Factory, typename Token>
auto retry_open(basic_client<Exec> &client, Factory &&factory, Token &&token) requires
	concepts::retry_open_request_factory<Factory> and concepts::retry_open_token<Exec,Token> and
	(not std::same_as<std::remove_cvref_t<Factory>,connect_request>)
{
	return retry_open(client, std::forward<Factory>(factory),
		retry_open_options{}, std::forward<Token>(token)
	);
}

} //namespace riwo::websocket

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_WEBSOCKET_DETAIL_RETRY_H
