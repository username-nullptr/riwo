// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_PROTOCOL_PARSER_H
#define RIWO_WEBSOCKET_PROTOCOL_PARSER_H

#include <riwo/websocket/protocol/types.h>

namespace riwo::websocket
{

struct frame_parse_result
{
	size_t consumed = 0;
	mutable_buffer payload {};
	uint64_t payload_offset = 0;

	bool header_ready = false;
	bool frame_finished = false;
};

class RIWO_WEBSOCKET_API frame_parser
{
	RIWO_DISABLE_COPY(frame_parser)

public:
	explicit frame_parser(frame_codec_config config);
	~frame_parser();

	frame_parser(frame_parser &&other) noexcept;
	frame_parser &operator=(frame_parser &&other) noexcept;

public:
	[[nodiscard]] sys_expected<frame_parse_result> parse(const mutable_buffer &input) noexcept;
	[[nodiscard]] const frame_header &header() const noexcept;

	[[nodiscard]] uint64_t payload_remaining() const noexcept;
	[[nodiscard]] frame_codec_config config() const noexcept;

	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] error_code last_error() const noexcept;

	frame_parser &reset() noexcept;

private:
	class impl;
	impl *m_impl;
};

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<close_payload_view> decode_close_payload(const const_buffer &payload) noexcept;

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_PROTOCOL_PARSER_H
