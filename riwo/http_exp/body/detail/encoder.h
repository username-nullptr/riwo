// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_ENCODER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_ENCODER_H

#include <riwo/http_exp/body/source.h>

namespace riwo::http_exp::detail
{

[[nodiscard]] RIWO_HTTP_EXP_API
body_source_ptr make_gzip_body_source(body_source_ptr source);

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_BODY_DETAIL_ENCODER_H
