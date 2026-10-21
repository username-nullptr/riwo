// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_ENGINE_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_ENGINE_H

#include <riwo/http_exp/protocol/engine.h>
#include <riwo/http_exp/protocol/limits.h>

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

namespace riwo::http_exp::v2
{

struct connection_config
{
	protocol_role role = protocol_role::client;
	message_limits limits {};
	uint32_t initial_stream_window = 65'535;
	uint32_t initial_connection_window = 1024 * 1024;
	uint32_t max_concurrent_streams = 100;
	uint32_t max_frame_size = 16 * 1024;
	size_t max_dynamic_table_size = 4 * 1024;
	size_t max_pending_output = 8 * 1024 * 1024;
	bool enable_extended_connect = false;
};

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

class RIWO_HTTP_EXP_API connection_engine
{
	RIWO_DISABLE_COPY(connection_engine)

public:
	explicit connection_engine(connection_config config);
	connection_engine(connection_engine &&) noexcept;
	connection_engine &operator=(connection_engine &&) noexcept;
	~connection_engine();

	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] sys_expected<ingest_result> ingest(
		const_buffer input, protocol_event_handler_t events) noexcept;
	[[nodiscard]] sys_expected<ingest_result> finish_input(
		protocol_event_handler_t events) noexcept;
	[[nodiscard]] sys_expected<void> submit(protocol_action_t action) noexcept;
	[[nodiscard]] sys_expected<produce_result> produce(
		mutable_buffer output) noexcept;
	[[nodiscard]] bool wants_write() const noexcept;

	[[nodiscard]] optional<int32_t> wire_stream_id(
		stream_token_t stream) const noexcept;
	[[nodiscard]] optional<stream_token_t> stream_from_wire_id(
		int32_t stream_id) const noexcept;
	[[nodiscard]] connection_diagnostics diagnostics() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl;
};

static_assert(protocol_engine<connection_engine>);

} //namespace riwo::http_exp::v2

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT

#endif //RIWO_HTTP_EXP_PROTOCOL_V2_ENGINE_H
