// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp/detail/stream/stream_transport.h>
#include <riwo/websocket_exp/detail/stream/send_engine.h>

namespace riwo::websocket_exp::detail
{

stream_transport::stream_transport() noexcept = default;

stream_transport::~stream_transport() = default;

void stream_transport::reset(http_exp::duplex_channel_io_ptr connection) noexcept
{
	m_connection = std::move(connection);
}

awaitable<std::tuple<error_code,size_t>>
stream_transport::async_read(std::shared_ptr<std::vector<std::byte>> storage)
{
	auto connection = m_connection;
	auto token = asio::as_tuple(asio::use_awaitable);

	co_return co_await asio::async_initiate<decltype(token),void(error_code,size_t)>(
	[connection = std::move(connection), storage = std::move(storage)]
	(auto next_handler) mutable
	{
		connection->read_some(mutable_buffer(storage->data(), storage->size()),
			io_handler_t(std::move(next_handler))
		);
	},
	token);
}

void stream_transport::start_write(const send_engine_owner_ptr &owner,
	const prepared_frame &frame, wire_frame_kind kind, const send_operation_ptr &operation) noexcept
{
	try {
		const auto header_size = frame.header_size;
		const auto payload_size = frame.payload_size;
		const auto application_size = frame.application_size;

		auto wire_lifetime = operation ?
			std::shared_ptr<std::vector<std::byte>>{} : frame.wire;

		prepared_frame_buffers buffers(frame);

		io_handler_t handler([owner, wire_lifetime = std::move(wire_lifetime),
			header_size, payload_size, application_size, kind, operation]
		(error_code error, size_t wire_size) mutable
		{
			ignore_unused(wire_lifetime);
			owner->send_side().complete_wire_frame (
				header_size, payload_size, application_size,
				kind, operation, error, wire_size
			);
		});
		m_connection->write(buffers.get(), std::move(handler));
	}
	catch(...)
	{
		owner->send_side().complete_wire_frame(frame.header_size,
			frame.payload_size, frame.application_size, kind,
			operation, exception_error(std::current_exception()), 0
		);
	}
}

} //namespace riwo::websocket_exp::detail
