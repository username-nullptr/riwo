// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_PARAMETERS_H
#define RIWO_EXP_HTTP_PROTOCOL_PARAMETERS_H

#include <riwo/experimental/http/global.h>
#include <riwo/core/value.h>

namespace riwo::experimental::http
{

struct parameter
{
	std::string name;
	riwo::value value;

	friend bool operator== (
		const parameter&, const parameter&
	) = default;
};
// An owning, immutable-at-the-exchange-boundary parameter collection.  It
// preserves input order and duplicate query keys.
class RIWO_EXP_HTTP_API parameter_view
{
public:
	using container_t = std::vector<parameter>;
	using const_iterator = container_t::const_iterator;

	parameter_view() = default;
	explicit parameter_view(container_t values) noexcept;

public:
	[[nodiscard]] optional<value> find_first(core_concepts::text_p<char> auto &&name) const;
	[[nodiscard]] std::vector<value> find_all(core_concepts::text_p<char> auto &&name) const;
	[[nodiscard]] bool contains(core_concepts::text_p<char> auto &&name) const noexcept;

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;
	[[nodiscard]] const container_t &values() const noexcept;

private:
	[[nodiscard]] optional<value> find_first_impl(std::string_view name) const;
	[[nodiscard]] std::vector<value> find_all_impl(std::string_view name) const;
	[[nodiscard]] bool contains_impl(std::string_view name) const noexcept;
	container_t m_values {};
};

[[nodiscard]] RIWO_EXP_HTTP_API optional<std::string>
decode_uri_component(std::string_view value, bool plus_as_space = false) noexcept;

[[nodiscard]] RIWO_EXP_HTTP_API optional<parameter_view>
parse_query_parameters(std::string_view query) noexcept;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/protocol/detail/parameters.h>


#endif //RIWO_EXP_HTTP_PROTOCOL_PARAMETERS_H
