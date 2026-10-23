// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V1_ENGINE_H
#define RIWO_HTTP_EXP_PROTOCOL_V1_ENGINE_H

#include <riwo/http_exp/protocol/engine.h>
#include <riwo/http_exp/protocol/v1/codec.h>

namespace riwo::http_exp::v1
{

struct connection_config
{
	protocol_role role = protocol_role::client;
	protocol version = protocol::v1_1;
	message_limits limits {};
	size_t max_pending_output = 8 * 1024 * 1024;
	request_target_form target_form = request_target_form::origin;
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

private:
	class impl;
	std::unique_ptr<impl> m_impl;
};

static_assert(concepts::protocol_engine<connection_engine>);

} //namespace riwo::http_exp::v1

#endif //RIWO_HTTP_EXP_PROTOCOL_V1_ENGINE_H
