// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_PROCESS_IO_H
#define RIWO_UTILS_DETAIL_PROCESS_IO_H

#include <riwo/utils/global.h>
#include <riwo/core/async_expected.h>

namespace riwo::utils::detail
{

template <typename Token, typename Initiation>
[[nodiscard]] RIWO_UTILS_TAPI auto initiate_process_io(Initiation initiation, Token &&token)
{
	using token_t = std::remove_cvref_t<Token>;
	token_t completion_token(std::forward<Token>(token));

	return asio::async_initiate<token_t,void(error_code,size_t)>(
		std::move(initiation), completion_token
	);
}

[[nodiscard]] RIWO_UTILS_API
std::shared_ptr<std::string> copy_process_buffer(const const_buffer &buffer);

} //namespace riwo::utils::detail


#endif //RIWO_UTILS_DETAIL_PROCESS_IO_H
