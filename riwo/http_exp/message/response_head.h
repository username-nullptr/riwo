// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_MESSAGE_RESPONSE_HEAD_H
#define RIWO_HTTP_EXP_MESSAGE_RESPONSE_HEAD_H

#include <riwo/http_exp/message/status.h>
#include <riwo/http_exp/message/fields.h>

namespace riwo::http_exp
{

struct response_head
{
	http_exp::status status {http_exp::status::ok};
	http_exp::fields headers {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_MESSAGE_RESPONSE_HEAD_H
