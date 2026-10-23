// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_PIPE_H
#define RIWO_HTTP_EXP_BODY_PIPE_H

#include <riwo/http_exp/body/file_sink.h>
#include <riwo/http_exp/body/reader.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec, typename Sink, typename Token = use_sync_t>
requires concepts::body_sink_model<std::remove_cvref_t<Sink>> and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>
[[nodiscard]] auto pipe(basic_body_reader<Exec> &reader, Sink &&sink,
	Token &&token = {});

} //namespace riwo::http_exp

#include <riwo/http_exp/body/detail/pipe.h>

#endif //RIWO_HTTP_EXP_BODY_PIPE_H
