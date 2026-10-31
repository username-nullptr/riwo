// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_DETAIL_C_FILE_POSITION_H
#define RIWO_EXP_HTTP_BODY_DETAIL_C_FILE_POSITION_H

#include <riwo/experimental/http/cxx/attributes.h>

namespace riwo::experimental::http::detail
{

[[nodiscard]] RIWO_EXP_HTTP_API int c_file_seek(std::FILE *file, int64_t offset, int origin) noexcept;
[[nodiscard]] RIWO_EXP_HTTP_API int64_t c_file_tell(std::FILE *file) noexcept;

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_BODY_DETAIL_C_FILE_POSITION_H
