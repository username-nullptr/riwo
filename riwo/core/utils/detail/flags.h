// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_FLAGS_H
#define RIWO_CORE_UTILS_DETAIL_FLAGS_H

namespace riwo
{

template <concepts::flag_template Enum>
constexpr flags<Enum>::flags(enum_t f) noexcept :
	m_value(static_cast<uint32_t>(f))
{

}

template <concepts::flag_template Enum>
constexpr flags<Enum>::flags(std::initializer_list<enum_t> flag_values) noexcept :
	m_value(initializer_list_helper(flag_values.begin(), flag_values.end()))
{

}

template <concepts::flag_template Enum>
template <concepts::flag_number Int>
flags<Enum> &flags<Enum>::operator&=(Int mask) noexcept
{
	m_value &= static_cast<uint32_t>(mask);
	return *this;
}

template <concepts::flag_template Enum>
flags<Enum> &flags<Enum>::operator&=(enum_t mask) noexcept
{
	m_value &= static_cast<uint32_t>(mask);
	return *this;
}

template <concepts::flag_template Enum>
flags<Enum> &flags<Enum>::operator|=(flags f) noexcept
{
	m_value |= f.m_value;
	return *this;
}

template <concepts::flag_template Enum>
flags<Enum> &flags<Enum>::operator|=(enum_t f) noexcept
{
	m_value |= static_cast<uint32_t>(f);
	return *this;
}

template <concepts::flag_template Enum>
flags<Enum> &flags<Enum>::operator^=(flags f) noexcept
{
	m_value ^= f.m_value;
	return *this;
}

template <concepts::flag_template Enum>
flags<Enum> &flags<Enum>::operator^=(enum_t f) noexcept
{
	m_value ^= static_cast<uint32_t>(f);
	return *this;
}

template <concepts::flag_template Enum>
template <concepts::flag_number Int>
constexpr flags<Enum> flags<Enum>::operator&(Int mask) const noexcept
{
	return flags( static_cast<enum_t>(m_value & static_cast<uint32_t>(mask)));
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator&(enum_t f) const noexcept
{
	return flags( static_cast<enum_t>(m_value & static_cast<uint32_t>(f)) );
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator|(flags f) const noexcept
{
	return flags( static_cast<enum_t>(m_value | f.m_value) );
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator|(enum_t f) const noexcept
{
	return flags( static_cast<enum_t>(m_value | static_cast<uint32_t>(f)) );
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator^(flags f) const noexcept
{
	return flags( static_cast<enum_t>(m_value ^ f.m_value) );
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator^(enum_t f) const noexcept
{
	return flags( static_cast<enum_t>(m_value ^ static_cast<uint32_t>(f)) );
}

template <concepts::flag_template Enum>
constexpr flags<Enum> flags<Enum>::operator~() const noexcept
{
	return flags( static_cast<enum_t>(~m_value) );
}

template <concepts::flag_template Enum>
constexpr bool flags<Enum>::operator!() const noexcept
{
	return not m_value;
}

template <concepts::flag_template Enum>
constexpr auto flags<Enum>::operator<=>(enum_t f) const noexcept
{
	return m_value <=> static_cast<uint32_t>(f);
}

template <concepts::flag_template Enum>
constexpr bool flags<Enum>::operator==(enum_t f) const noexcept
{
	return m_value == static_cast<uint32_t>(f);
}

template <concepts::flag_template Enum>
template <concepts::arithmetic T>
constexpr flags<Enum>::operator T() const noexcept
{
	return value<T>();
}

template <concepts::flag_template Enum>
constexpr flags<Enum>::operator enum_t() const noexcept
{
	return static_cast<enum_t>(m_value);
}

template <concepts::flag_template Enum>
constexpr uint32_t flags<Enum>::operator*() const noexcept
{
	return value();
}

template <concepts::flag_template Enum>
template <concepts::arithmetic T>
constexpr T flags<Enum>::value() const noexcept
{
	return static_cast<T>(m_value);
}

template <concepts::flag_template Enum>
constexpr bool flags<Enum>::test_flag(enum_t f) const noexcept
{
	auto _f = static_cast<uint32_t>(f);
	return (m_value & _f) == _f and (_f != 0 or m_value == _f);
}

template <concepts::flag_template Enum>
constexpr flags<Enum> &flags<Enum>::set_flag(enum_t f, bool on) noexcept
{
	return on ? (*this |= f) : (*this &= ~static_cast<uint32_t>(f));
}

template <concepts::flag_template Enum>
constexpr uint32_t flags<Enum>::initializer_list_helper(iterator it, iterator end) noexcept
{
	return it == end ? 0U : static_cast<uint32_t>(*it) | initializer_list_helper(it + 1, end);
}

template <concepts::flag_template Enum>
[[nodiscard]] bool operator==(const flags<Enum> &flags, Enum flag) noexcept
{
	return flags.template value<uint32_t>() == static_cast<uint32_t>(flag);
}

template <concepts::flag_template Enum>
[[nodiscard]] bool operator==(Enum flag, const flags<Enum> &flags) noexcept
{
	return operator==(flags, flag);
}

template <concepts::flag_template Enum>
[[nodiscard]] bool operator!=(const flags<Enum> &flags, Enum flag) noexcept
{
	return not operator==(flags, flag);
}

template <concepts::flag_template Enum>
[[nodiscard]] bool operator!=(Enum flag, const flags<Enum> &flags) noexcept
{
	return operator!=(flags, flag);
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_FLAGS_H
