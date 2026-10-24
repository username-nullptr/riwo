// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H
#define RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H

#include <riwo/http_exp/cxx/attributes.h>

namespace riwo::http_exp::detail
{

[[nodiscard]] RIWO_HTTP_EXP_API int c_file_seek(std::FILE *file, int64_t offset, int origin) noexcept;
[[nodiscard]] RIWO_HTTP_EXP_API int64_t c_file_tell(std::FILE *file) noexcept;

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H
