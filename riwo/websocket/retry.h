// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_RETRY_H
#define RIWO_WEBSOCKET_RETRY_H

#include <riwo/websocket/client.h>

namespace riwo::websocket
{

enum class retry_open_event : uint8_t {
	opening, waiting,
};

struct retry_open_context
{
	// The open attempt represented by this context. Zero means that no open
	// attempt has completed yet.
	size_t attempt = 0;
	error_code error {};

	url endpoint {};
	optional<http::status_enum> http_status {};
};

struct RIWO_WEBSOCKET_API retry_open_decision
{
	bool retry = false;
	std::chrono::milliseconds delay {0};

	[[nodiscard]] static
	retry_open_decision stop() noexcept;

	[[nodiscard]] static
	retry_open_decision retry_after(std::chrono::milliseconds delay) noexcept;
};

using retry_open_decider = std::function<retry_open_decision (
	const retry_open_context&, std::chrono::milliseconds
)>;

using retry_open_observer = std::function<void (
	retry_open_event, const retry_open_context&
)>;

struct retry_open_options
{
	std::chrono::milliseconds initial_delay {500};
	std::chrono::milliseconds max_delay {30000};

	double multiplier = 2.0;
	double jitter = 0.2;

	// Total number of open attempts. Zero intentionally means unbounded.
	size_t max_attempts = 0;

	retry_open_decider decide {};
	retry_open_observer observe {};
};

template <core_concepts::exec Exec = asio::any_io_executor>
struct RIWO_WEBSOCKET_TAPI basic_retry_open_result
{
	using executor_type = Exec;
	using executor_t = executor_type;

	using stream_t = basic_stream<executor_t>;
	using diagnostics_t = basic_open_diagnostics<executor_t>;

	explicit basic_retry_open_result(executor_t exec) :
		stream(std::move(exec)) {}

	[[nodiscard]] executor_t get_executor() const noexcept {
		return stream.get_executor();
	}
	stream_t stream;
	size_t attempts = 0;

	diagnostics_t diagnostics {};
	retry_open_context last_failure {};
};

using retry_open_result = basic_retry_open_result<>;

[[nodiscard]] RIWO_WEBSOCKET_API std::chrono::milliseconds suggest_retry_open_delay (
	const retry_open_options &options, size_t failed_attempts
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API retry_open_decision default_retry_open_decision (
	const retry_open_context &context, std::chrono::milliseconds suggested_delay
) noexcept;

namespace concepts
{

template <typename Func>
concept retry_open_request_factory = []() consteval -> bool
{
	if constexpr( not std::invocable<std::remove_reference_t<Func>&,const retry_open_context&> )
		return false;
	else
	{
		using return_t = std::invoke_result_t <
			std::remove_reference_t<Func>&, const retry_open_context&
		>;
		if constexpr( is_awaitable_v<return_t> )
			return std::same_as<awaitable_ret_t<return_t>,connect_request>;
		else
			return std::same_as<return_t,connect_request>;
	}
}();

template <typename Exec, typename Token>
concept retry_open_token = core_concepts::exec<Exec> and
	core_concepts::async_tf_opt_token<Token,error_code,basic_retry_open_result<Exec>> and
	not is_detached_v<token_unbound_t<Token>>;

} //namespace concepts

template <typename Exec, typename Token = const use_awaitable_t&>
[[nodiscard]] auto retry_open (
	basic_client<Exec> &client, connect_request request, retry_open_options options,
	Token &&token = use_awaitable
)
requires concepts::retry_open_token<Exec,Token>;

template <typename Exec, typename Token = const use_awaitable_t&>
[[nodiscard]] auto retry_open (
	basic_client<Exec> &client, connect_request request,
	Token &&token = use_awaitable
)
requires concepts::retry_open_token<Exec,Token>;

template <typename Exec, typename Factory, typename Token = const use_awaitable_t&>
[[nodiscard]] auto retry_open (
	basic_client<Exec> &client, Factory &&factory, retry_open_options options,
	Token &&token = use_awaitable
) requires
	concepts::retry_open_request_factory<Factory> and concepts::retry_open_token<Exec,Token> and
	(not std::same_as<std::remove_cvref_t<Factory>,connect_request>);

template <typename Exec, typename Factory, typename Token = const use_awaitable_t&>
[[nodiscard]] auto retry_open (
	basic_client<Exec> &client, Factory &&factory,
	Token &&token = use_awaitable
) requires
	concepts::retry_open_request_factory<Factory> and concepts::retry_open_token<Exec,Token> and
	(not std::same_as<std::remove_cvref_t<Factory>,connect_request>);

} //namespace riwo::websocket
#include <riwo/websocket/detail/retry.h>


#endif //RIWO_WEBSOCKET_RETRY_H
