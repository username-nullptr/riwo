// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_METHOD_H
#define RIWO_HTTP_EXP_PROTOCOL_METHOD_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

struct method_properties
{
	bool safe = false;
	bool idempotent = false;

	friend constexpr bool operator==(const method_properties &,
		const method_properties &) noexcept = default;
};

class RIWO_HTTP_EXP_API method
{
public:
	enum well_known { get, head, post, put, patch, delete_, options, trace, connect };

	method(well_known value) noexcept;
	explicit method(std::string token);
	method(std::string token, method_properties properties);

	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] optional<well_known> known() const noexcept;
	[[nodiscard]] bool is_safe() const noexcept;
	[[nodiscard]] bool is_idempotent() const noexcept;

	friend RIWO_HTTP_EXP_API bool operator==(
		const method &lhs, const method &rhs) noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

template <>
struct std::hash<riwo::http_exp::method>
{
	[[nodiscard]] size_t operator()(
		const riwo::http_exp::method &value) const noexcept;
};

#endif //RIWO_HTTP_EXP_PROTOCOL_METHOD_H
