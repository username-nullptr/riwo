// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_UNSUPPORTED_H
#define RIWO_HTTP_EXP_BODY_DETAIL_UNSUPPORTED_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp::detail
{

template <typename Value, core_concepts::exec Exec>
[[nodiscard]] Value unsupported_value(const Exec &exec)
{
	if constexpr( std::constructible_from<Value,const Exec&> )
		return Value(exec);
	else
		return Value{};
}

template <typename Value, core_concepts::exec Exec, typename Token>
[[nodiscard]] auto unsupported_result(const Exec &exec, Token &&token)
{
	auto error = make_error_code(errc::operation_not_supported);
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error(
			sys_expected<Value>(sys_unexpected(error)), token);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw(
			sys_expected<Value>(sys_unexpected(error)));
	}
	else
	{
		return initiate_io<Value>(exec, [error, exec](auto handler) mutable {
			std::move(handler)(error, unsupported_value<Value>(exec));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec, typename Token>
auto unsupported_operation(const Exec &exec, Token &&token)
{
	auto error = make_error_code(errc::operation_not_supported);
	if constexpr( is_error_code_token_v<Token> )
	{
		token = error;
		return;
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		throw std::system_error(std::error_code(error));
	}
	else
	{
		return initiate_io_void(exec, [error](auto handler) mutable {
			std::move(handler)(error);
		}, std::forward<Token>(token));
	}
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_BODY_DETAIL_UNSUPPORTED_H
