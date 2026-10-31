// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_V2_ENGINE_H
#define RIWO_EXP_HTTP_PROTOCOL_V2_ENGINE_H

#include <riwo/experimental/http/protocol/engine.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/protocol/v2/extension.h>
#include <typeinfo>

#if RIWO_EXP_HTTP_HTTP2_SUPPORT

namespace riwo::experimental::http::v2
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

class RIWO_EXP_HTTP_API connection_engine
{
	RIWO_DISABLE_COPY(connection_engine)

public:
	explicit connection_engine(const connection_config &config);
	~connection_engine();

	connection_engine(connection_engine &&) noexcept;
	connection_engine &operator=(connection_engine &&) noexcept;

public:
	[[nodiscard]] sys_expected<ingest_result> ingest (
		const const_buffer &input, protocol_event_handler_t events
	) noexcept;

	[[nodiscard]] sys_expected<ingest_result> finish_input(
		protocol_event_handler_t events
	) noexcept;

	[[nodiscard]] sys_expected<void> submit(protocol_action_t action) noexcept;
	[[nodiscard]] sys_expected<produce_result> produce(const mutable_buffer &output) noexcept;

	[[nodiscard]] optional<int32_t> wire_stream_id(stream_token_t stream) const noexcept;
	[[nodiscard]] optional<stream_token_t> stream_from_wire_id(int32_t stream_id) const noexcept;

public:
	[[nodiscard]] connection_diagnostics diagnostics() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] bool wants_write() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	[[nodiscard]] void *protocol_extension(const std::type_info &type) noexcept;
	class impl;
	std::unique_ptr<impl> m_impl {};
};

static_assert(concepts::protocol_engine<connection_engine>);

} //namespace riwo::experimental::http::v2
#include <riwo/experimental/http/protocol/v2/detail/engine.h>

#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
#endif //RIWO_EXP_HTTP_PROTOCOL_V2_ENGINE_H
