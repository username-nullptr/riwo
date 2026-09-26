// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_COMPRESSION_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_COMPRESSION_H

#include <riwo/http/global.h>

namespace riwo::http
{

constexpr size_t default_max_decoded_body_size_v = 64 * 1024 * 1024;

[[nodiscard]] RIWO_HTTP_API double content_coding_quality (
	std::string_view value, std::string_view coding
) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
is_precompressed_mime_type(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_API bool
is_compressible_mime_type(std::string_view value) noexcept;

class RIWO_HTTP_API gzip_encoder final
{
	RIWO_DISABLE_COPY(gzip_encoder)

public:
	explicit gzip_encoder(int level = -1) noexcept;
	~gzip_encoder();

	gzip_encoder(gzip_encoder &&other) noexcept;
	gzip_encoder &operator=(gzip_encoder &&other) noexcept;

	[[nodiscard]] sys_expected<std::string>
	append(std::string_view data, bool finish = false) noexcept;

	[[nodiscard]] bool finished() const noexcept;

private:
	class impl;
	impl *m_impl;
};

class RIWO_HTTP_API gzip_decoder final
{
	RIWO_DISABLE_COPY(gzip_decoder)

public:
	explicit gzip_decoder (
		size_t max_output_size = default_max_decoded_body_size_v
	) noexcept;

	~gzip_decoder();

	gzip_decoder(gzip_decoder &&other) noexcept;
	gzip_decoder &operator=(gzip_decoder &&other) noexcept;

	[[nodiscard]] sys_expected<std::string>
	append(std::string_view data, bool finish = false) noexcept;

	[[nodiscard]] bool finished() const noexcept;
	[[nodiscard]] size_t output_size() const noexcept;

private:
	class impl;
	impl *m_impl;
};

[[nodiscard]] RIWO_HTTP_API sys_expected<std::string>
gzip_compress(std::string_view data, int level = -1) noexcept;

[[nodiscard]] RIWO_HTTP_API sys_expected<std::string>
gzip_decompress (std::string_view data, size_t max_output_size = default_max_decoded_body_size_v) noexcept;

} //namespace riwo::http

#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_COMPRESSION_H
