// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_SPEC_H
#define RIWO_HTTP_EXP_BODY_SPEC_H

#include <riwo/http_exp/message/fields.h>

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

	enum class mode { empty, known_length, streaming };

	[[nodiscard]] static body_spec empty();
	[[nodiscard]] static body_spec known(uint64_t size, trailer_spec trailers = {});
	[[nodiscard]] static body_spec streaming(trailer_spec trailers = {});

	[[nodiscard]] mode type() const noexcept;
	[[nodiscard]] optional<uint64_t> length() const noexcept;
	[[nodiscard]] const trailer_spec &trailers() const noexcept;

private:
	body_spec(mode type, optional<uint64_t> length, trailer_spec trailers);
	mode type_;
	optional<uint64_t> length_;
	trailer_spec trailers_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_SPEC_H
