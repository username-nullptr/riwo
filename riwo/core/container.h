// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CONTAINER_H
#define RIWO_CORE_CONTAINER_H

#include <riwo/core/value.h>

namespace riwo
{

using key_t = std::string;

using kv_vector = std::vector<std::pair<std::string,value>>;

class RIWO_CORE_API parameter_map : public kv_vector
{
public:
	using kv_vector::kv_vector;
	using kv_vector::operator[];

	[[nodiscard]] iterator find(std::string_view key);
	[[nodiscard]] const_iterator find(std::string_view key) const;

	value &operator[](std::string_view key);
	value &operator[](std::string &&key);
	[[nodiscard]] const value &operator[](std::string_view key) const;
};

template <typename Derived>
class RIWO_CORE_TAPI const_parameters
{
public:
	using derived_t = crtp_derived_t<Derived,const_parameters>;
	using parameters_t = parameter_map;
	using value_t = value;

public:
	explicit const_parameters(const parameters_t *parameters);
	virtual ~const_parameters() = default;

	[[nodiscard]] optional<value_t> parameter (
		const concepts::text_p<char> auto &key
	) const noexcept;

	[[nodiscard]] bool contains_parameter (
		const concepts::text_p<char> auto &key, const value_t &expected_value
	) const noexcept;

	[[nodiscard]] bool contains_parameter (
		const concepts::text_p<char> auto &key
	) const noexcept;

	[[nodiscard]] optional<value_t> parameter(size_t index) const;
	[[nodiscard]] bool contains_parameter(size_t index) const noexcept;

	[[nodiscard]] const parameters_t &parameters() const noexcept;

protected:
	const parameters_t *m_parameters = nullptr;
};

template <typename Derived>
class RIWO_CORE_TAPI mutable_parameters : public const_parameters<Derived>
{
	using base_t = const_parameters<Derived>;

public:
	template <concepts::text_p<char> T>
	base_t::derived_t &set_parameter(T &&key, base_t::value_t parameter_value) noexcept;

	template <concepts::text_p<char> T>
	base_t::derived_t &unset_parameter(const T &key) noexcept;

	[[nodiscard]] base_t::parameters_t &parameters() noexcept;
	using base_t::parameters;
	using base_t::base_t;
};

} //namespace riwo
#include <riwo/core/detail/container.h>


#endif //RIWO_CORE_CONTAINER_H
