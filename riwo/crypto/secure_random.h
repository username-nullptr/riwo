// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_SECURE_RANDOM_H
#define RIWO_CRYPTO_SECURE_RANDOM_H

#include <riwo/crypto/global.h>
#include <riwo/core/sys_expected.h>

namespace riwo::crypto
{

[[nodiscard]] RIWO_CRYPTO_API
sys_expected<> secure_random_bytes(const mutable_buffer &output) noexcept;

} //namespace riwo::crypto


#endif //RIWO_CRYPTO_SECURE_RANDOM_H
