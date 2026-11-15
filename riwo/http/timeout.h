// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_TIMEOUT_H
#define RIWO_HTTP_TIMEOUT_H

#include <riwo/http/global.h>

namespace riwo::http
{

struct timeout_policy
{
	milliseconds connect   {30000};
	milliseconds header    {30000};
	milliseconds body_idle {30000};

	optional<milliseconds> overall {};
	milliseconds pool_wait {30000};
};

} //namespace riwo::http


#endif //RIWO_HTTP_TIMEOUT_H
