// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_CLOSE_OPERATIONS_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_CLOSE_OPERATIONS_H

#include <riwo/websocket/types.h>

namespace riwo::websocket::detail
{

struct close_wait_operation
{
	uint64_t id = 0;

	asio::any_completion_handler <
		void(error_code,close_info)
	> completion {};
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_CLOSE_OPERATIONS_H
