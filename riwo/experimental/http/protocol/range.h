// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_RANGE_H
#define RIWO_EXP_HTTP_PROTOCOL_RANGE_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

struct byte_range_spec
{
	enum class form {
		closed, open_ended, suffix
	};
	form type = form::closed;
	uint64_t first = 0;
	uint64_t last = 0;
};

struct range_specifier
{
	std::string unit {};
	std::vector<byte_range_spec> ranges {};
};

struct resolved_byte_range
{
	uint64_t offset = 0;
	uint64_t length = 0;

	friend bool operator== (
		const resolved_byte_range&,
		const resolved_byte_range&
	) = default;
};

struct content_range
{
	std::string unit {};
	bool satisfied = false;

	uint64_t first = 0;
	uint64_t last = 0;

	optional<uint64_t> complete_length {};

	[[nodiscard]] uint64_t length() const noexcept;
};

[[nodiscard]] RIWO_EXP_HTTP_API  sys_expected<range_specifier>
parse_range_header(std::string_view value) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API  std::vector<resolved_byte_range>
resolve_byte_ranges(const range_specifier &specifier, uint64_t complete_length) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API sys_expected<content_range>
parse_content_range(std::string_view value) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API std::string
format_content_range(const resolved_byte_range &range, uint64_t complete_length);

[[nodiscard]] RIWO_EXP_HTTP_API std::string
format_unsatisfied_content_range(uint64_t complete_length);

[[nodiscard]] RIWO_EXP_HTTP_API sys_expected<std::string>
parse_multipart_byte_ranges_boundary(std::string_view content_type) noexcept;

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_RANGE_H
