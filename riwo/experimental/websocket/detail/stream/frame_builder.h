// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_WEBSOCKET_DETAIL_STREAM_FRAME_BUILDER_H
#define RIWO_EXP_WEBSOCKET_DETAIL_STREAM_FRAME_BUILDER_H

#include <riwo/experimental/websocket/detail/permessage_deflate.h>
#include <riwo/experimental/websocket/types.h>

namespace riwo::experimental::websocket::detail
{

struct prepared_frame
{
	std::shared_ptr<std::vector<std::byte>> wire {};
	std::shared_ptr<std::vector<std::byte>> payload_owner {};

	std::array<std::byte,14> header {};
	const_buffer payload {};
	std::vector<const_buffer> additional_payloads {};

	size_t header_size = 0;
	size_t payload_size = 0;
	size_t application_size = 0;
};

class RIWO_EXP_WEBSOCKET_API prepared_frame_buffers
{
public:
	explicit prepared_frame_buffers(const prepared_frame &frame);
	[[nodiscard]] std::span<const const_buffer> get() const noexcept;

private:
	std::array<const_buffer,2> m_inline {};
	std::vector<const_buffer> m_overflow {};
	std::span<const const_buffer> m_buffers {};
};

struct RIWO_EXP_WEBSOCKET_API prepared_message
{
	optional<prepared_frame> first {};
	std::vector<prepared_frame> additional {};

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

	[[nodiscard]] prepared_frame &front() noexcept;
	[[nodiscard]] const prepared_frame &front() const noexcept;

	[[nodiscard]] prepared_frame &operator[](size_t index) noexcept;
	[[nodiscard]] const prepared_frame &operator[](size_t index) const noexcept;

	void reserve(size_t count);
	void push_back(prepared_frame frame);
};

class RIWO_EXP_WEBSOCKET_API frame_builder
{
	RIWO_DISABLE_COPY_MOVE(frame_builder)

public:
	frame_builder() noexcept = default;

	frame_builder(role local_role, const stream_config &config,
		std::span<const extension> extensions = {}) noexcept;

public:
	frame_builder &reset(role local_role, const stream_config &config,
		std::span<const extension> extensions = {}) noexcept;

	[[nodiscard]] sys_expected<prepared_frame> prepare_control (
		opcode op, const const_buffer &payload, bool borrow_payload = false
	) const noexcept;

	[[nodiscard]] sys_expected<prepared_frame> prepare_close (
		const close_frame &frame
	) const noexcept;

	[[nodiscard]] sys_expected<prepared_message> prepare_message (
		message_type type, std::span<const const_buffer> buffers, write_options options = {}
	) const noexcept;

	[[nodiscard]] sys_expected<prepared_frame> prepare_data_frame (
		message_type type, const const_buffer &payload,
		bool continuation, bool fin, bool borrow_payload = false
	) const noexcept;

private:
	role m_role = role::client;

	size_t m_max_frame_size = stream_config{}.max_frame_size;
	size_t m_max_message_size = stream_config{}.max_message_size;
	size_t m_fragment_size = stream_config{}.write_fragment_size;

	compression_config m_compression_config {};
	permessage_deflate_runtime m_compression {};

	mutable permessage_deflater m_deflater {};
	error_code m_deflater_error {};
};

} //namespace riwo::experimental::websocket::detail


#endif //RIWO_EXP_WEBSOCKET_DETAIL_STREAM_FRAME_BUILDER_H
