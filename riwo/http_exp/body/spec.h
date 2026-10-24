// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_SPEC_H
#define RIWO_HTTP_EXP_BODY_SPEC_H

#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API body_spec
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
	class impl;
	std::shared_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_SPEC_H
