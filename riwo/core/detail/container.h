// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_CONTAINER_H
#define RIWO_CORE_DETAIL_CONTAINER_H

namespace riwo
{

template <typename Derived>
const_parameters<Derived>::const_parameters(const parameters_t *parameters) :
	m_parameters(parameters)
{

}

template <typename Derived>
auto const_parameters<Derived>::parameter
(const concepts::text_p<char> auto &key) const noexcept -> optional<value_t>
{
	auto it = parameters().find(strtls::to_string(key));
	if( it == parameters().end() )
		return nullopt;
	return it->second;
}

template <typename Derived>
bool const_parameters<Derived>::contains_parameter
(const concepts::text_p<char> auto &key, const value_t &expected_value) const noexcept
{
	auto it = parameters().find(strtls::to_string(key));
	if( it != parameters().end() )
		return it->second == expected_value;
	return false;
}

template <typename Derived>
bool const_parameters<Derived>::contains_parameter
(const concepts::text_p<char> auto &key) const noexcept
{
	auto it = parameters().find(strtls::to_string(key));
	return it != parameters().end();
}

template <typename Derived>
auto const_parameters<Derived>::parameter(size_t index) const -> optional<value_t>
{
	if( not contains_parameter(index) )
		runtime_error::loc_throw("index out of range.");
	return parameters()[index].second;
}

template <typename Derived>
bool const_parameters<Derived>::contains_parameter(size_t index) const noexcept
{
	return index < parameters().size();
}

template <typename Derived>
auto const_parameters<Derived>::parameters() const noexcept -> const parameters_t&
{
	return *m_parameters;
}

template <typename Derived>
template <concepts::text_p<char> T>
auto mutable_parameters<Derived>::set_parameter
(T &&key, typename base_t::value_t parameter_value) noexcept -> base_t::derived_t &
{
	parameters()[strtls::to_string(std::forward<T>(key))] = std::move(parameter_value);
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
template <concepts::text_p<char> T>
auto mutable_parameters<Derived>::unset_parameter(const T &key) noexcept -> base_t::derived_t&
{
	auto it = parameters().find(strtls::to_string(key));
	if( it != parameters().end() )
		parameters().erase(it);
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
auto mutable_parameters<Derived>::parameters() noexcept -> base_t::parameters_t&
{
	return remove_const(*this->m_parameters);
}

} //namespace riwo


#endif //RIWO_CORE_DETAIL_CONTAINER_H
