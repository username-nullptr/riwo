// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_EXTENSION_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_EXTENSION_H

#include <cstddef>
#include <cstdint>

namespace riwo::http_exp::v2
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

class stream_extension
{
public:
	constexpr stream_extension() noexcept = default;
	constexpr explicit stream_extension(int32_t stream_id) noexcept :
		m_stream_id(stream_id)
	{
	}

	[[nodiscard]] constexpr int32_t stream_id() const noexcept {
		return m_stream_id;
	}

private:
	int32_t m_stream_id = -1;
};

class connection_extension
{
public:
	constexpr connection_extension() noexcept = default;
	constexpr explicit connection_extension(
		const connection_diagnostics &diagnostics) noexcept :
		m_diagnostics(diagnostics)
	{
	}

	[[nodiscard]] constexpr const settings_snapshot &
	local_settings() const noexcept {
		return m_diagnostics.local_settings;
	}

	[[nodiscard]] constexpr const settings_snapshot &
	peer_settings() const noexcept {
		return m_diagnostics.peer_settings;
	}

	[[nodiscard]] constexpr size_t pending_output() const noexcept {
		return m_diagnostics.pending_output;
	}

	[[nodiscard]] constexpr bool draining() const noexcept {
		return m_diagnostics.draining;
	}

	[[nodiscard]] constexpr const connection_diagnostics &
	diagnostics() const noexcept {
		return m_diagnostics;
	}

private:
	connection_diagnostics m_diagnostics {};
};

} //namespace riwo::http_exp::v2

#endif //RIWO_HTTP_EXP_PROTOCOL_V2_EXTENSION_H
