// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_BASE64_H
#define RIWO_CRYPTO_BASE64_H

#include <riwo/crypto/global.h>
#include <span>

namespace riwo::crypto
{

[[nodiscard]] RIWO_CRYPTO_API
std::string base64_encode(std::span<const std::byte> input);

[[nodiscard]] RIWO_CRYPTO_API
std::string base64_encode(std::string_view input);

} //namespace riwo::crypto


#endif //RIWO_CRYPTO_BASE64_H
