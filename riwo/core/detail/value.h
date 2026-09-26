// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_VALUE_H
#define RIWO_CORE_DETAIL_VALUE_H

namespace riwo
{

template <concepts::character CharT, typename Traits, class Alloc>
basic_value<CharT,Traits,Alloc>::basic_value(concepts::value_set<char_t> auto &&arg)
{
	set(std::forward<decltype(arg)>(arg));
}

template <concepts::character CharT, typename Traits, class Alloc>
template <typename Arg0, typename...Args>
basic_value<CharT,Traits,Alloc>::basic_value
(format_string<Arg0,Args...> fmt, Arg0 &&arg0, Args&&...args) :
	basic_value(std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...))
{

}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::to_string() & noexcept -> string_t&
{
	return get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::to_string() const & noexcept -> const string_t&
{
	return get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::to_string() && noexcept -> string_t
{
	return std::move(*this).get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
basic_value<CharT,Traits,Alloc>::operator string_t&() & noexcept
{
	return to_string();
}

template <concepts::character CharT, typename Traits, typename Alloc>
basic_value<CharT,Traits,Alloc>::operator const string_t&() const & noexcept
{
	return to_string();
}

template <concepts::character CharT, typename Traits, typename Alloc>
basic_value<CharT,Traits,Alloc>::operator string_t() && noexcept
{
	return std::move(*this).to_string();
}

template <concepts::character CharT, typename Traits, typename Alloc>
template <typename T, typename...Args>
decltype(auto) basic_value<CharT,Traits,Alloc>::get(Args&&...args) &
	requires concepts::value_get<T,CharT,Args...>
{
	return value_serializer<std::remove_cvref_t<T>,char_t>()
		.get(*this, std::forward<Args>(args)...);
}

template <concepts::character CharT, typename Traits, typename Alloc>
template <typename T, typename...Args>
decltype(auto) basic_value<CharT,Traits,Alloc>::get(Args&&...args) &&
	requires concepts::value_get<T,CharT,Args...>
{
	return value_serializer<std::remove_cvref_t<T>,char_t>()
		.get(std::move(*this), std::forward<Args>(args)...);
}

template <concepts::character CharT, typename Traits, typename Alloc>
template <typename T, typename...Args>
decltype(auto) basic_value<CharT,Traits,Alloc>::get(Args&&...args) const &
	requires concepts::value_get<T,CharT,Args...>
{
	return value_serializer<std::remove_cvref_t<T>,char_t>()
		.get(*this, std::forward<Args>(args)...);
}

template <concepts::character CharT, typename Traits, typename Alloc>
template <typename T, typename...Args>
decltype(auto) basic_value<CharT,Traits,Alloc>::get(Args&&...args) const &&
	requires concepts::value_get<T,CharT,Args...>
{
	return value_serializer<std::remove_cvref_t<T>,char_t>()
		.get(std::move(*this), std::forward<Args>(args)...);
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::get() & noexcept -> string_t&
{
	return m_str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::get() const & noexcept -> const string_t&
{
	return m_str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::get() && noexcept -> string_t&&
{
	return std::move(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<bool> basic_value<CharT,Traits,Alloc>::to_bool(size_t base) const noexcept
{
	return get<bool>(base);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<int32_t> basic_value<CharT,Traits,Alloc>::to_int(size_t base) const noexcept
{
	return get<int32_t>(base);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<uint32_t> basic_value<CharT,Traits,Alloc>::to_uint(size_t base) const noexcept
{
	return get<uint32_t>(base);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<int64_t> basic_value<CharT,Traits,Alloc>::to_long(size_t base) const noexcept
{
	return get<int64_t>(base);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<uint64_t> basic_value<CharT,Traits,Alloc>::to_ulong(size_t base) const noexcept
{
	return get<uint64_t>(base);
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<float> basic_value<CharT,Traits,Alloc>::to_float() const noexcept
{
	return get<float>();
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<double> basic_value<CharT,Traits,Alloc>::to_double() const noexcept
{
	return get<double>();
}

template <concepts::character CharT, typename Traits, typename Alloc>
optional<long double> basic_value<CharT,Traits,Alloc>::to_ldouble() const noexcept
{
	return get<long double>();
}

template <concepts::character CharT, typename Traits, typename Alloc>
template <typename Arg0, typename...Args>
basic_value<CharT,Traits,Alloc>&
basic_value<CharT,Traits,Alloc>::set(format_string<Arg0, Args...> fmt, Arg0 &&arg0, Args&&...args)
{
	m_str = std::format(fmt, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
	return *this;
}

template <concepts::character CharT, typename Traits, typename Alloc>
basic_value<CharT,Traits,Alloc>&
basic_value<CharT,Traits,Alloc>::set(concepts::value_set<char_t> auto &&arg)
{
	using Arg = decltype(arg);
	using arg_t = std::remove_cvref_t<Arg>;
	m_str = value_serializer<arg_t,char_t>().set(std::forward<Arg>(arg));
	return *this;
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::is_alpha() const noexcept
{
	return strtls::is_alpha(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::is_digit() const noexcept
{
	return strtls::is_digit(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::is_rlnum() const noexcept
{
	return strtls::is_rlnum(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::is_alnum() const noexcept
{
	return strtls::is_alnum(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::is_ascii() const noexcept
{
	return strtls::is_ascii(m_str);
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator*() & noexcept -> string_t&
{
	return get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator*() const & noexcept -> const string_t&
{
	return get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator*() && noexcept -> string_t
{
	return std::move(*this).get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator->() noexcept -> string_t*
{
	return &get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
const basic_value<CharT,Traits,Alloc>::string_t*
basic_value<CharT,Traits,Alloc>::operator->() const noexcept
{
	return &get();
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::operator==(const str_view_t &str) const
{
	return m_str == str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::operator==(const string_t &str) const
{
	return m_str == str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
bool basic_value<CharT,Traits,Alloc>::operator==(const char_t *str) const
{
	return m_str == str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator<=>(const basic_value &other) const
{
	return m_str <=> other.to_string();
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator<=>(const str_view_t &str) const
{
	return m_str <=> str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator<=>(const string_t &str) const
{
	return m_str <=> str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
auto basic_value<CharT,Traits,Alloc>::operator<=>(const char_t *str) const
{
	return m_str <=> str;
}

template <concepts::character CharT, typename Traits, typename Alloc>
basic_value<CharT,Traits,Alloc> &basic_value<CharT,Traits,Alloc>::operator=
(concepts::value_set<char_t> auto &&arg)
{
	set(std::forward<decltype(arg)>(arg));
	return *this;
}

} //namespace riwo

namespace std
{

template <riwo::concepts::character CharT, typename...Args>
struct hash<riwo::basic_value<CharT,Args...>>
{
	size_t operator()(const riwo::basic_value<CharT,Args...> &v) const noexcept {
		return hash<std::basic_string<CharT,Args...>>()(v);
	}
};

template <riwo::concepts::character CharT>
struct formatter<riwo::basic_value<CharT>, CharT>
{
	auto format(const riwo::basic_value<CharT> &value, auto &context) const {
		return m_formatter.format(value.to_string(), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<std::basic_string<CharT>, CharT> m_formatter;
};

} //namespace std


#endif //RIWO_CORE_DETAIL_VALUE_H
