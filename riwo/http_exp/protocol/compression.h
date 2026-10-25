// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_COMPRESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_COMPRESSION_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

constexpr size_t default_max_decoded_body_size_v = 64U * 1024U * 1024U;

[[nodiscard]] RIWO_HTTP_EXP_API
double content_coding_quality(std::string_view value, std::string_view coding) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
bool is_precompressed_mime_type(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
bool is_compressible_mime_type(std::string_view value) noexcept;

class RIWO_HTTP_EXP_API gzip_encoder
{
	RIWO_DISABLE_COPY(gzip_encoder)

public:
	explicit gzip_encoder(int level = -1) noexcept;
	~gzip_encoder();

	gzip_encoder(gzip_encoder&&) noexcept;
	gzip_encoder &operator=(gzip_encoder&&) noexcept;

	[[nodiscard]] sys_expected<std::string>
	append(std::string_view data, bool finish = false) noexcept;

	[[nodiscard]] bool finished() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

class RIWO_HTTP_EXP_API gzip_decoder
{
	RIWO_DISABLE_COPY(gzip_decoder)
public:
	explicit gzip_decoder(size_t max_output_size = default_max_decoded_body_size_v) noexcept;
	~gzip_decoder();

	gzip_decoder(gzip_decoder&&) noexcept;
	gzip_decoder &operator=(gzip_decoder&&) noexcept;

	[[nodiscard]] sys_expected<std::string>
	append(std::string_view data, bool finish = false) noexcept;

	[[nodiscard]] bool finished() const noexcept;
	[[nodiscard]] size_t output_size() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<std::string> gzip_compress(std::string_view data, int level = -1) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API sys_expected<std::string> gzip_decompress (
	std::string_view data, size_t max_output_size = default_max_decoded_body_size_v
) noexcept;

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_COMPRESSION_H
