// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_GLOBAL_H
#define RIWO_CORE_GLOBAL_H

#include <riwo/core/cxx.h>
#include <riwo/core/asio.h>

namespace riwo
{

[[nodiscard]] RIWO_CORE_API const char *version_string();

[[nodiscard]] RIWO_CORE_API const char *text_code();

RIWO_CORE_API std::thread::id this_thread_id();

[[noreturn]] RIWO_CORE_API void forced_termination();

} //namespace riwo


#endif //RIWO_CORE_GLOBAL_H
