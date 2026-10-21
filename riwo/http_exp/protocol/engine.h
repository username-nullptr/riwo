// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_ENGINE_H
#define RIWO_HTTP_EXP_PROTOCOL_ENGINE_H

#include <riwo/http_exp/protocol/event.h>

namespace riwo::http_exp
{

enum class protocol_role { client, server };

struct ingest_result
{
	size_t consumed = 0;
	bool paused = false;
	bool wants_write = false;
	bool needs_input = false;
};

struct produce_result
{
	size_t produced = 0;
	bool has_more = false;
};

template<class Engine>
concept protocol_engine = requires(Engine &engine, const_buffer input,
	mutable_buffer output, protocol_action action,
	protocol_event_handler events)
{
	{ engine.selected_protocol() } -> std::same_as<http_exp::protocol>;
	{ engine.ingest(input, events) } ->
		std::same_as<sys_expected<ingest_result>>;
	{ engine.finish_input(events) } ->
		std::same_as<sys_expected<ingest_result>>;
	{ engine.submit(std::move(action)) } ->
		std::same_as<sys_expected<void>>;
	{ engine.produce(output) } ->
		std::same_as<sys_expected<produce_result>>;
	{ engine.wants_write() } noexcept -> std::same_as<bool>;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_ENGINE_H
