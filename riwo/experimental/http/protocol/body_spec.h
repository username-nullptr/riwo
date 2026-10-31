// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_BODY_SPEC_H
#define RIWO_EXP_HTTP_PROTOCOL_BODY_SPEC_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

class RIWO_EXP_HTTP_API body_spec
{
public:
	struct trailer_spec
	{
		std::vector<std::string> names;
		friend bool operator==(const trailer_spec &, const trailer_spec &) = default;
	};
	enum class mode {
		empty, known_length, streaming, head
	};
	body_spec(const body_spec &) = default;
	body_spec &operator=(const body_spec &) = default;

	body_spec(body_spec &&other) noexcept;
	body_spec &operator=(body_spec &&other) noexcept;

public:
	[[nodiscard]] static body_spec empty();
	[[nodiscard]] static body_spec known(uint64_t size, trailer_spec trailers = {});
	[[nodiscard]] static body_spec streaming(trailer_spec trailers = {});
	[[nodiscard]] static body_spec head(uint64_t content_length);

	[[nodiscard]] mode type() const noexcept;
	[[nodiscard]] optional<uint64_t> length() const noexcept;
	[[nodiscard]] const trailer_spec &trailers() const noexcept;

private:
	body_spec(mode type,
		optional<uint64_t> length, trailer_spec trailers
	);
	mode m_type = mode::empty;
	optional<uint64_t> m_length {};
	trailer_spec m_trailers {};
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_BODY_SPEC_H
