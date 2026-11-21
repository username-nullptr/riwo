// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_HMAC_SHA256_H
#define RIWO_CRYPTO_HMAC_SHA256_H

#include <riwo/crypto/sha256.h>

namespace riwo::crypto
{

[[nodiscard]] RIWO_CRYPTO_API
sha256::digest_t hmac_sha256(span_t<> key, span_t<> message);

[[nodiscard]] RIWO_CRYPTO_API
sha256::digest_t hmac_sha256(std::string_view key, std::string_view message);

// Compares the full 32-byte tag without a content-dependent early exit.
[[nodiscard]] RIWO_CRYPTO_API
bool hmac_sha256_verify(span_t<> key, span_t<> message, span_t<> tag);

[[nodiscard]] RIWO_CRYPTO_API
bool hmac_sha256_verify(std::string_view key, std::string_view message, span_t<> tag);

} //namespace riwo::crypto


#endif //RIWO_CRYPTO_HMAC_SHA256_H
