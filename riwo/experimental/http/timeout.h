// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TIMEOUT_H
#define RIWO_EXP_HTTP_TIMEOUT_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

struct timeout_policy
{
	milliseconds connect   {30000};
	milliseconds header    {30000};
	milliseconds body_idle {30000};

	optional<milliseconds> overall {};
	milliseconds pool_wait {30000};
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_TIMEOUT_H
