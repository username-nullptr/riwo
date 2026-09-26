// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_BUFFER_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_BUFFER_H

#include <riwo/websocket/protocol/parser.h>
#include <riwo/websocket/protocol/detail/utf8.h>
#include <riwo/websocket/detail/permessage_deflate.h>
#include <riwo/websocket/types.h>

namespace riwo::websocket::detail
{

struct received_event
{
	opcode op = opcode::binary;

	optional<message> data {};
	optional<data_frame> frame {};

	optional<message_chunk> chunk {};
	std::vector<std::byte> chunk_storage {};
	std::vector<std::byte> control {};
};

enum class receive_target : uint8_t {
	message, frame, chunk,
};

class RIWO_WEBSOCKET_API receive_buffer
{
	RIWO_DISABLE_COPY_MOVE(receive_buffer)

public:
	receive_buffer() = default;

	void reset(role local_role, const stream_config &config,
		std::span<const extension> extensions, std::vector<std::byte> pending_data
	);
	[[nodiscard]] mutable_buffer available_data() noexcept;

	[[nodiscard]] std::shared_ptr<std::vector<std::byte>> read_storage() const noexcept;
	[[nodiscard]] error_code commit_read(size_t size) noexcept;

	[[nodiscard]] error_code target_error(receive_target target) const noexcept;
	[[nodiscard]] sys_expected<optional<received_event>> consume(receive_target target) noexcept;

private:
	std::vector<std::byte> m_pending_data {};
	size_t m_pending_offset = 0;

	std::unique_ptr<frame_parser> m_parser {};
	std::shared_ptr<std::vector<std::byte>> m_read_buffer {};

	size_t m_read_size = 0;
	size_t m_read_offset = 0;
	size_t m_max_message_size = 0;

	bool m_permessage_deflate = false;
	bool m_message_compressed = false;

	permessage_deflate_runtime m_compression {};
	permessage_inflater m_inflater {};

	optional<message_type> m_message_type {};
	optional<receive_target> m_message_target {};
	std::vector<std::byte> m_message_body {};

	size_t m_message_wire_size = 0;
	size_t m_message_size = 0;
	bool m_chunk_first = true;

	optional<utf8_validator> m_chunk_utf8 {};
	size_t m_frame_offset = 0;

	std::vector<std::byte> m_control_body {};
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_RECEIVE_BUFFER_H
