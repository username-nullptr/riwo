// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_READ_IPP
#define RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_READ_IPP

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_H
# error "Include <riwo/websocket/detail/stream/impl.h> instead."
#endif

namespace riwo::websocket
{

template <core_concepts::exec Exec>
template <typename Buffer>
basic_message<Buffer> basic_stream<Exec>::impl::convert_message(message value, error_code &error) noexcept
{
	try {
		auto type = value.type;
		if constexpr( std::same_as<Buffer, std::vector<std::byte>> )
		{
			error.clear();
			return {.type = type, .body = std::move(value.body)};
		}
		else
		{
			auto body = copy_buffer_data<Buffer>(std::move(value.body));
			error.clear();
			return {.type = type, .body = std::move(body)};
		}
	}
	catch(const std::bad_alloc&) {
		error = make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {
		error = make_system_error_code(std::errc::io_error);
	}
	return {};
}

template <core_concepts::exec Exec>
template <typename Buffer>
basic_data_frame<Buffer> basic_stream<Exec>::impl::convert_frame
(data_frame value, error_code &error) noexcept
{
	try {
		auto type = value.type;
		auto fin = value.fin;
		auto continuation = value.continuation;

		if constexpr( std::same_as<Buffer, std::vector<std::byte>> )
		{
			error.clear();
			return {
				.type = type, .body = std::move(value.body),
				.continuation = continuation,
				.fin = fin,
			};
		}
		else
		{
			auto body = copy_buffer_data<Buffer>(std::move(value.body));
			error.clear();
			return {
				.type = type, .body = std::move(body),
				.continuation = continuation,
				.fin = fin,
			};
		}
	}
	catch(const std::bad_alloc&) {
		error = make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {
		error = make_system_error_code(std::errc::io_error);
	}
	return {};
}

template <core_concepts::exec Exec>
message basic_stream<Exec>::impl::read(error_code &error) noexcept
{
	return m_receive_engine.read(error);
}

template <core_concepts::exec Exec>
data_frame basic_stream<Exec>::impl::read_frame(error_code &error) noexcept
{
	return m_receive_engine.read_frame(error);
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_read_message(message_handler_t handler)
{
	m_receive_engine.async_read_message(std::move(handler));
}

template <core_concepts::exec Exec>
void basic_stream<Exec>::impl::async_read_frame(frame_handler_t handler)
{
	m_receive_engine.async_read_frame(std::move(handler));
}

template <core_concepts::exec Exec>
template <typename Consumer>
message_info basic_stream<Exec>::impl::consume(Consumer &&consumer, error_code &error) noexcept
{
	return m_receive_engine.consume(std::forward<Consumer>(consumer), error);
}

template <core_concepts::exec Exec>
template <typename Consumer>
void basic_stream<Exec>::impl::async_consume(Consumer &&consumer, info_handler_t handler)
{
	m_receive_engine.async_consume(std::forward<Consumer>(consumer),
		std::move(handler)
	);
}

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_IMPL_READ_IPP
