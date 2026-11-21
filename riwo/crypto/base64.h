// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_BASE64_H
#define RIWO_CRYPTO_BASE64_H

#include <riwo/crypto/global.h>
#include <riwo/core/optional.h>
#include <span>

namespace riwo::crypto
{

[[nodiscard]] RIWO_CRYPTO_API
std::string base64_encode(std::span<const std::byte> input);

[[nodiscard]] RIWO_CRYPTO_API
std::string base64_encode(std::string_view input);

// Invalid input returns an empty optional. A valid empty input decodes to an empty string.
// Standard Base64 requires padding for partial groups.
[[nodiscard]] RIWO_CRYPTO_API
optional<std::string> base64_decode(std::string_view input);

[[nodiscard]] RIWO_CRYPTO_API
std::string base64url_encode(std::span<const std::byte> input, bool padded = false);

[[nodiscard]] RIWO_CRYPTO_API
std::string base64url_encode(std::string_view input, bool padded = false);

// Accepts padded and unpadded Base64URL.
[[nodiscard]] RIWO_CRYPTO_API
optional<std::string> base64url_decode(std::string_view input);

} //namespace riwo::crypto


#endif //RIWO_CRYPTO_BASE64_H
