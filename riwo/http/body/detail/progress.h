// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_BODY_DETAIL_PROGRESS_H
#define RIWO_HTTP_BODY_DETAIL_PROGRESS_H

#include <riwo/http/body/reader.h>

namespace riwo::http::detail
{

using body_progress_handler = std::function<void(
	uint64_t transferred, uint64_t total
)>;

[[nodiscard]] RIWO_HTTP_API
body_reader_state_ptr make_progress_body_reader (
	body_reader_state_ptr source, uint64_t total, body_progress_handler handler
);

} //namespace riwo::http::detail


#endif //RIWO_HTTP_BODY_DETAIL_PROGRESS_H
