// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_METHOD_H
#define RIWO_HTTP_EXP_PROTOCOL_METHOD_H

#include <riwo/http_exp/global.h>
#include <riwo/core/cxx/string_concepts.h>

namespace riwo::http_exp
{

struct method_properties
{
	bool safe = false;
	bool idempotent = false;

	friend constexpr bool operator== (
		const method_properties&, const method_properties&
	) noexcept = default;
};

class RIWO_HTTP_EXP_API method
{
public:
	enum well_known {
		get, head, post, put, patch, delete_, options, trace, connect,

		Get     = get    ,
		Head    = head   ,
		Post    = post   ,
		Put     = put    ,
		Patch   = patch  ,
		Delete  = delete_,
		Options = options,
		Trace   = trace  ,
		Connect = connect,
	};

public:
	method(well_known value) noexcept;

	explicit method(core_concepts::string_p<char> auto &&token);
	method(core_concepts::string_p<char> auto &&token, method_properties properties);

	method(const method &other) = default;
	method &operator=(const method &other) = default;

	method(method &&other) noexcept;
	method &operator=(method &&other) noexcept;

public:
	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] optional<well_known> known() const noexcept;

	[[nodiscard]] bool is_safe() const noexcept;
	[[nodiscard]] bool is_idempotent() const noexcept;

	friend RIWO_HTTP_EXP_API bool operator== (
		const method &lhs, const method &rhs
	) noexcept;

private:
	void initialize(std::string token);
	void initialize(std::string token, method_properties properties);

	std::string m_token {};
	method_properties m_properties {};
	optional<well_known> m_known {};
};

} //namespace riwo::http_exp
#include <riwo/http_exp/protocol/detail/method.h>


#endif //RIWO_HTTP_EXP_PROTOCOL_METHOD_H
