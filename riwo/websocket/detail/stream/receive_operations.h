// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H

#include <riwo/websocket/types.h>

namespace riwo::websocket::detail
{

struct read_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,message)> completion {};
	asio::cancellation_signal cancellation {};
};

struct frame_read_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,data_frame)> completion {};
	asio::cancellation_signal cancellation {};
};

struct consume_wait_operation
{
	uint64_t id = 0;
	asio::any_completion_handler<void(error_code,message_info)> completion {};
	asio::cancellation_signal cancellation {};
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_OPERATIONS_H
