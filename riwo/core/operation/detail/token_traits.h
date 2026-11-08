// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_DETAIL_TOKEN_TRAITS_H
#define RIWO_CORE_OPERATION_DETAIL_TOKEN_TRAITS_H

#include <riwo/core/cxx/tools.h>

namespace riwo
{

template <typename Token>
constexpr decltype(auto) unbound_token(Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;

	if constexpr( is_redirect_time_v<token_t> )
		return unbound_token(token.token);
	else if constexpr( is_redirect_error_v<token_t> )
		return return_reference(token.token_);
	else if constexpr( is_cancellation_slot_binder_v<token_t> )
		return token.get();
	else
		return std::forward<Token>(token);
}

} //namespace riwo


#endif //RIWO_CORE_OPERATION_DETAIL_TOKEN_TRAITS_H
