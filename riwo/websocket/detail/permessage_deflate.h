// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_PERMESSAGE_DEFLATE_H
#define RIWO_WEBSOCKET_DETAIL_PERMESSAGE_DEFLATE_H

#include <riwo/websocket/types.h>

namespace riwo::websocket::detail
{

struct permessage_deflate_parameters
{
	bool server_no_context_takeover = false;
	bool client_no_context_takeover = false;

	optional<uint8_t> server_max_window_bits {};
	optional<uint8_t> client_max_window_bits {};

	bool client_max_window_bits_present = false;
};

struct permessage_deflate_runtime
{
	bool enabled = false;
	bool outgoing_no_context_takeover = true;
	bool incoming_no_context_takeover = true;

	uint8_t outgoing_window_bits = 15;
	uint8_t incoming_window_bits = 15;
};

class RIWO_WEBSOCKET_API permessage_inflater
{
	RIWO_DISABLE_COPY(permessage_inflater)

public:
	permessage_inflater() noexcept;
	~permessage_inflater();

	permessage_inflater(permessage_inflater&&) noexcept;
	permessage_inflater &operator=(permessage_inflater&&) noexcept;

	[[nodiscard]] error_code reset (
		uint8_t window_bits, bool no_context_takeover
	) noexcept;

	[[nodiscard]] sys_expected<std::vector<std::byte>> inflate (
		std::span<const std::byte> payload, size_t max_message_size
	) noexcept;

	[[nodiscard]] sys_expected<std::vector<std::byte>> inflate_chunk (
		std::span<const std::byte> payload, bool final, size_t max_output_size
	) noexcept;

private:
	struct impl;
	std::unique_ptr<impl> m_impl {};
};

[[nodiscard]] RIWO_WEBSOCKET_API bool
supported_extension_offers(std::span<const extension> extensions) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API bool
supported_negotiated_extensions(std::span<const extension> extensions) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API bool supported_extension_response (
	std::span<const extension> response, std::span<const extension> offers
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<extension>
negotiate_permessage_deflate(const extension &offer, const extension &policy) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<permessage_deflate_runtime>
make_permessage_deflate_runtime(std::span<const extension> negotiated_extensions, role local_role) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<std::vector<std::byte>>
deflate_message (
	std::span<const const_buffer> buffers, uint8_t window_bits = 15,
	int compression_level = -1
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API sys_expected<std::vector<std::byte>>
inflate_message (
	std::span<const std::byte> payload, size_t max_message_size,
	uint8_t window_bits = 15
) noexcept;

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_PERMESSAGE_DEFLATE_H
