// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_SOURCE_H
#define RIWO_HTTP_EXP_BODY_SOURCE_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp::concepts
{

template <typename Source>
concept body_source_model = requires(Source &source, mutable_buffer buffer,
	asio::any_completion_handler<void(error_code, size_t)> read_handler_t,
	asio::any_completion_handler<void(error_code)> rewind_handler_t)
{
	{ source.size() } noexcept -> std::same_as<optional<uint64_t>>;
	{ source.replayable() } noexcept -> std::same_as<bool>;
	{ source.read_some(buffer, std::move(read_handler_t)) } -> std::same_as<void>;
	{ source.rewind(std::move(rewind_handler_t)) } -> std::same_as<void>;
	{ source.cancel() } noexcept -> std::same_as<void>;
};

template <typename Source>
concept body_source_model_p =
	body_source_model<std::remove_cvref_t<Source>>;

template <typename Source>
concept finalizable_body_source_model = requires
(Source &source, error_code &error, asio::any_completion_handler<void(error_code)> finish_handler)
{
	{ source.finish(error) } -> std::same_as<void>;
	{ source.finish(std::move(finish_handler)) } -> std::same_as<void>;
};

template <typename Source>
concept finalizable_body_source_model_p =
	finalizable_body_source_model<std::remove_cvref_t<Source>>;

} // namespace riwo::http_exp::concepts
#include <riwo/http_exp/body/file_source.h>
#include <riwo/http_exp/body/memory.h>

namespace riwo::http_exp
{

static_assert(concepts::body_source_model<memory_body_source>);
static_assert(concepts::body_source_model<file_body_source>);
static_assert(concepts::finalizable_body_source_model<file_body_source>);

} //namespace riwo::http_exp
#include <riwo/http_exp/body/detail/source.h>


#endif //RIWO_HTTP_EXP_BODY_SOURCE_H
