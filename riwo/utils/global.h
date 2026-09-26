// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_GLOBAL_H
#define RIWO_UTILS_GLOBAL_H

#include <riwo/utils/cxx/configs.h>
#include <riwo/core/global.h>

#if RIWO_BUILD_STATIC
# define RIWO_UTILS_API
#elif defined(riwo_utils_EXPORTS)
# define RIWO_UTILS_API  RIWO_DECL_EXPORT
#else //riwo_utils_EXPORTS
# define RIWO_UTILS_API  RIWO_DECL_IMPORT
#endif //riwo_utils_EXPORTS

#define RIWO_UTILS_VAPI
#define RIWO_UTILS_TAPI

namespace riwo::utils
{

[[nodiscard]] RIWO_UTILS_API asio::thread_pool &thread_pool() noexcept;

} //namespace riwo::utils


#endif //RIWO_UTILS_GLOBAL_H
