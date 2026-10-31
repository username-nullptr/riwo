// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_DETAIL_ENCODER_H
#define RIWO_EXP_HTTP_BODY_DETAIL_ENCODER_H

#include <riwo/experimental/http/body/source.h>

namespace riwo::experimental::http::detail
{

[[nodiscard]] RIWO_EXP_HTTP_API
body_source_ptr make_gzip_body_source(body_source_ptr source);

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_BODY_DETAIL_ENCODER_H
