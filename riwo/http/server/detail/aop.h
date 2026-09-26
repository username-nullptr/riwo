// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_AOP_H
#define RIWO_HTTP_SERVER_DETAIL_AOP_H

namespace riwo::http
{

template <core_concepts::exec Exec>
basic_aop<Exec>::~basic_aop() = default;

template <core_concepts::exec Exec>
awaitable<bool> basic_aop<Exec>::before(context_t &context)
{
	ignore_unused(context);
	co_return false;
}

template <core_concepts::exec Exec>
awaitable<bool> basic_aop<Exec>::after(context_t &context)
{
	ignore_unused(context);
	co_return false;
}

template <core_concepts::exec Exec>
bool basic_aop<Exec>::exception(context_t &context, const std::exception &ex)
{
	ignore_unused(context, ex);
	return false;
}

} //namespace riwo::http


#endif //RIWO_HTTP_SERVER_DETAIL_AOP_H
