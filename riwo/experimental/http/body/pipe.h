// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_PIPE_H
#define RIWO_EXP_HTTP_BODY_PIPE_H

#include <riwo/experimental/http/body/file_sink.h>
#include <riwo/experimental/http/body/reader.h>

namespace riwo::experimental::http
{

template <core_concepts::exec Exec,
	core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token = use_sync_t>
[[nodiscard]] auto pipe(basic_body_reader<Exec> &reader,
	concepts::body_sink_model_p auto &&sink, Token &&token = {}
);

} //namespace riwo::experimental::http
#include <riwo/experimental/http/body/detail/pipe.h>


#endif //RIWO_EXP_HTTP_BODY_PIPE_H
