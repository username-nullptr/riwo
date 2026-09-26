// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/stream_transport.h>
#include <riwo/websocket/detail/stream/send_engine.h>
#include <riwo/http/utils/connection.h>

namespace riwo::websocket::detail
{

stream_transport::stream_transport() noexcept = default;

stream_transport::~stream_transport() = default;

void stream_transport::reset
(std::shared_ptr<void> connection, read_fn_t read_fn, write_fn_t write_fn) noexcept
{
	m_connection = std::move(connection);
	m_read_fn = read_fn;
	m_write_fn = write_fn;
}

awaitable<std::tuple<error_code,size_t>>
stream_transport::async_read(std::shared_ptr<std::vector<std::byte>> storage)
{
	auto connection = m_connection;
	auto read_fn = m_read_fn;

	co_return co_await http::detail::initiate_connection_io(
	[connection = std::move(connection), storage = std::move(storage), read_fn]
	(auto next_handler) mutable
	{
		read_fn(connection.get(),
			mutable_buffer(storage->data(), storage->size()),
			io_handler_t(std::move(next_handler))
		);
	},
	asio::as_tuple(asio::use_awaitable));
}

void stream_transport::start_write(const std::shared_ptr<send_engine_owner> &owner,
	prepared_frame frame, wire_frame_kind kind, const std::shared_ptr<send_operation> &operation) noexcept
{
	try {
		auto buffers = std::span<const const_buffer>(frame.buffers);
		io_handler_t handler([owner, frame, kind, operation](error_code error, size_t wire_size) mutable
		{
			owner->send_side().complete_wire_frame (
				frame, kind, operation, error, wire_size
			);
		});
		m_write_fn(m_connection.get(), buffers, std::move(handler));
	}
	catch(...)
	{
		owner->send_side().complete_wire_frame(frame, kind,
			operation, exception_error(std::current_exception()), 0
		);
	}
}

} //namespace riwo::websocket::detail
