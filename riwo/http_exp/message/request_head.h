// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_MESSAGE_REQUEST_HEAD_H
#define RIWO_HTTP_EXP_MESSAGE_REQUEST_HEAD_H

#include <riwo/http_exp/message/method.h>
#include <riwo/http_exp/message/fields.h>
#include <riwo/core/url.h>

namespace riwo::http_exp
{

struct request_head
{
	http_exp::method method {http_exp::method::get};
	riwo::url url {};
	std::string scheme {};
	std::string authority {};
	std::string target {"/"};
	http_exp::fields headers {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_MESSAGE_REQUEST_HEAD_H
