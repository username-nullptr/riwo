// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_PARAMETERS_H
#define RIWO_HTTP_EXP_PROTOCOL_PARAMETERS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

struct parameter
{
	std::string name;
	std::string value;

	friend bool operator== (
		const parameter&, const parameter&
	) = default;
};
// An owning, immutable-at-the-exchange-boundary parameter collection.  It
// preserves input order and duplicate query keys.
class RIWO_HTTP_EXP_API parameter_view
{
public:
	using container_t = std::vector<parameter>;
	using const_iterator = container_t::const_iterator;

	parameter_view() = default;
	explicit parameter_view(container_t values) noexcept;

	[[nodiscard]] optional<std::string_view>
	find_first(std::string_view name) const noexcept;

	[[nodiscard]] std::vector<std::string_view>
	find_all(std::string_view name) const;

	[[nodiscard]] bool contains(std::string_view name) const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;
	[[nodiscard]] const container_t &values() const noexcept;

private:
	container_t m_values {};
};

[[nodiscard]] RIWO_HTTP_EXP_API optional<std::string>
decode_uri_component(std::string_view value, bool plus_as_space = false) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API optional<parameter_view>
parse_query_parameters(std::string_view query) noexcept;

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_PARAMETERS_H
