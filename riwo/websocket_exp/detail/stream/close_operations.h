// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_OPERATIONS_H
#define RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_OPERATIONS_H

#include <riwo/websocket_exp/types.h>

namespace riwo::websocket_exp::detail
{

struct close_wait_operation
{
	uint64_t id = 0;

	asio::any_completion_handler <
		void(error_code,close_info)
	> completion {};
};

using close_wait_operation_ptr = std::shared_ptr<close_wait_operation>;

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_OPERATIONS_H
