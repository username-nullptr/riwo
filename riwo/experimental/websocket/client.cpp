// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "client.h"

namespace riwo::experimental::websocket
{

connect_request::connect_request(url endpoint_value) :
	endpoint(std::move(endpoint_value))
{

}

} //namespace riwo::experimental::websocket
