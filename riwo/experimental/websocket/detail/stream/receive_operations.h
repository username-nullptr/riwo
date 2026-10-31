// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H
#define RIWO_EXP_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H

#include <riwo/experimental/websocket/types.h>

namespace riwo::experimental::websocket::detail
{

struct read_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,message)> completion {};
	asio::cancellation_signal cancellation {};
};

using read_wait_operation_ptr = std::shared_ptr<read_wait_operation>;

struct frame_read_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,data_frame)> completion {};
	asio::cancellation_signal cancellation {};
};

using frame_read_wait_operation_ptr = std::shared_ptr<frame_read_wait_operation>;

struct consume_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,message_info)> completion {};
	asio::cancellation_signal cancellation {};
};

using consume_wait_operation_ptr = std::shared_ptr<consume_wait_operation>;

} //namespace riwo::experimental::websocket::detail


#endif //RIWO_EXP_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H
