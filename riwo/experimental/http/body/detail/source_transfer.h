// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_DETAIL_SOURCE_TRANSFER_H
#define RIWO_EXP_HTTP_BODY_DETAIL_SOURCE_TRANSFER_H

#include <riwo/experimental/http/body/source.h>
#include <riwo/experimental/http/body/writer.h>

namespace riwo::experimental::http::detail
{

struct source_transfer_options
{
	optional<uint64_t> length;
	fields trailers;
	std::function<void(uint64_t)> progress;
};

[[nodiscard]] RIWO_EXP_HTTP_API uint64_t transfer_source_sync (
	body_source &source, body_writer_state &writer, source_transfer_options options,
	error_code &error
) noexcept;

RIWO_EXP_HTTP_API void transfer_source (
	body_source_ptr source, body_writer_state_ptr writer, source_transfer_options options,
	asio::any_completion_handler<void(error_code,uint64_t)> handler
);

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_BODY_DETAIL_SOURCE_TRANSFER_H
