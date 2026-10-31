// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_V2_EXTENSION_H
#define RIWO_EXP_HTTP_PROTOCOL_V2_EXTENSION_H

#include <riwo/experimental/http/cxx/attributes.h>

namespace riwo::experimental::http::v2
{

struct settings_snapshot
{
	uint32_t header_table_size = 4096;
	uint32_t enable_push = 0;

	uint32_t max_concurrent_streams = UINT32_MAX;
	uint32_t initial_window_size = 65'535;

	uint32_t max_frame_size = 16 * 1024;
	uint32_t max_header_list_size = UINT32_MAX;

	uint32_t enable_connect_protocol = 0;
};

struct connection_diagnostics
{
	settings_snapshot local_settings {};
	settings_snapshot peer_settings {};
	size_t pending_output = 0;
	bool draining = false;
};

class RIWO_EXP_HTTP_VAPI stream_extension
{
public:
	constexpr stream_extension() noexcept = default;
	constexpr explicit stream_extension(int32_t stream_id) noexcept;
	[[nodiscard]] constexpr int32_t stream_id() const noexcept;

private:
	int32_t m_stream_id = -1;
};

class RIWO_EXP_HTTP_VAPI connection_extension
{
public:
	constexpr connection_extension() noexcept = default;
	constexpr explicit connection_extension(const connection_diagnostics &diagnostics) noexcept;

	[[nodiscard]] constexpr settings_snapshot local_settings() const noexcept;
	[[nodiscard]] constexpr settings_snapshot peer_settings() const noexcept;

	[[nodiscard]] constexpr size_t pending_output() const noexcept;
	[[nodiscard]] constexpr bool draining() const noexcept;

	[[nodiscard]] constexpr connection_diagnostics diagnostics() const noexcept;

private:
	connection_diagnostics m_diagnostics {};
};

} //namespace riwo::experimental::http::v2
#include <riwo/experimental/http/protocol/v2/detail/extension.h>


#endif //RIWO_EXP_HTTP_PROTOCOL_V2_EXTENSION_H
