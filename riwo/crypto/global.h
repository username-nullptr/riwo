// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_GLOBAL_H
#define RIWO_CRYPTO_GLOBAL_H

#include <riwo/core/global.h>

#if RIWO_BUILD_STATIC
# define RIWO_CRYPTO_API
#elif defined(riwo_crypto_EXPORTS)
# define RIWO_CRYPTO_API RIWO_DECL_EXPORT
#else
# define RIWO_CRYPTO_API RIWO_DECL_IMPORT
#endif

#define RIWO_CRYPTO_VAPI RIWO_CORE_VAPI
#define RIWO_CRYPTO_TAPI RIWO_CORE_TAPI


#endif //RIWO_CRYPTO_GLOBAL_H
