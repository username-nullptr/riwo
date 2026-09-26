// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_RANGE_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_RANGE_H

#include <riwo/http/protocol/utils/core/body_norms.h>
#include <riwo/http/protocol/header.h>

namespace riwo::http
{

struct RIWO_HTTP_API byte_range_spec
{
	enum class form_t {
		closed, open_ended, suffix
	};
	form_t form = form_t::closed;
	size_t first = 0;
	size_t last = 0;
};

struct RIWO_HTTP_API ranges_specifier
{
	std::string unit {};
	std::vector<byte_range_spec> ranges {};
};

struct RIWO_HTTP_API content_range
{
	std::string unit {};
	bool satisfied = false;

	size_t first = 0;
	size_t last = 0;
	optional<size_t> complete_length {};

	[[nodiscard]] size_t length() const noexcept;
};

struct RIWO_HTTP_API byte_range_part
{
	http::headers fields {};
	content_range range {};
};

struct RIWO_HTTP_API byte_range_chunk
{
	size_t part_index = 0;
	size_t offset = 0;
	std::string data {};
};

[[nodiscard]] RIWO_HTTP_API sys_expected<ranges_specifier>
parse_range_header(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_API file_ranges
resolve_byte_ranges(const ranges_specifier &specifier, size_t complete_length) noexcept;

[[nodiscard]] RIWO_HTTP_API sys_expected<content_range>
parse_content_range(std::string_view value) noexcept;

[[nodiscard]] RIWO_HTTP_API std::string
format_content_range(const file_range &range, size_t complete_length);

[[nodiscard]] RIWO_HTTP_API std::string
format_unsatisfied_content_range(size_t complete_length);

[[nodiscard]] RIWO_HTTP_API sys_expected<std::string>
parse_multipart_byte_ranges_boundary(std::string_view content_type) noexcept;

class RIWO_HTTP_API multipart_byte_ranges_parser final
{
	RIWO_DISABLE_COPY(multipart_byte_ranges_parser)

public:
	explicit multipart_byte_ranges_parser(std::string boundary);
	~multipart_byte_ranges_parser();

	multipart_byte_ranges_parser(multipart_byte_ranges_parser &&other) noexcept;
	multipart_byte_ranges_parser &operator=(multipart_byte_ranges_parser &&other) noexcept;

	[[nodiscard]] sys_expected<std::vector<byte_range_chunk>>
	append(std::string_view data) noexcept;

	[[nodiscard]] error_code finish() const noexcept;
	[[nodiscard]] bool finished() const noexcept;
	[[nodiscard]] const std::vector<byte_range_part> &parts() const noexcept;

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_RANGE_H
