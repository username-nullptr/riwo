// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_MESSAGE_DETAIL_REQUEST_HEAD_H
#define RIWO_HTTP_EXP_MESSAGE_DETAIL_REQUEST_HEAD_H

#include <riwo/http_exp/message.h>

namespace riwo::http_exp::detail
{

struct request_head
{
	http_exp::method method {http_exp::method::get};
	riwo::url url {};
	http_exp::fields fields {};
};

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_MESSAGE_DETAIL_REQUEST_HEAD_H
