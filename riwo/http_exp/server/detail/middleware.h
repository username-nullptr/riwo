// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_MIDDLEWARE_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_MIDDLEWARE_H

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
awaitable<bool> basic_middleware<Exec>::before(exchange_t&) {
	co_return false;
}

template <core_concepts::exec Exec>
awaitable<bool> basic_middleware<Exec>::after(exchange_t&) {
	co_return false;
}

template <core_concepts::exec Exec>
bool basic_middleware<Exec>::exception(exchange_t&, const std::exception&) {
	return false;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_MIDDLEWARE_H
