// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CRYPTO_SHA256_H
#define RIWO_CRYPTO_SHA256_H

#include <riwo/crypto/global.h>

namespace riwo::crypto
{

class RIWO_CRYPTO_API sha256
{
public:
	static constexpr size_t block_size = 64;
	static constexpr size_t digest_size = 32;
	using digest_t = std::array<std::byte,digest_size>;

public:
	explicit sha256(span_t<> input);
	explicit sha256(std::string_view input);

	sha256();
	~sha256();

	sha256(const sha256 &other);
	sha256(sha256 &&other) noexcept;

	sha256 &operator=(const sha256 &other);
	sha256 &operator=(sha256 &&other) noexcept;

public:
	sha256 &append(uint8_t x);
	sha256 &append(char c);
	sha256 &append(const void *data, size_t size);

	sha256 &append(span_t<> input);
	sha256 &append(std::string_view input);

public:
	void operator+=(uint8_t x);
	void operator+=(char c);

	sha256 &operator+=(span_t<> input);
	sha256 &operator+=(std::string_view input);

public:
	// Finalization is idempotent. Appending after finalization throws logic_error.
	sha256 &finalize() noexcept;

	// These accessors return the digest of the current input without changing the hash.
	[[nodiscard]] digest_t digest() const noexcept;
	[[nodiscard]] std::string hex(bool upper_case = true) const;
	[[nodiscard]] std::string base64() const;

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo::crypto


#endif //RIWO_CRYPTO_SHA256_H
