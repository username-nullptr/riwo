// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_BYTE_ORDER_H
#define RIWO_CORE_UTILS_DETAIL_BYTE_ORDER_H

namespace riwo
{

inline bool is_little_endian()
{
	static constexpr uint32_t i = 0x12345678;
	return *reinterpret_cast<const char*>(&i) == 0x78;
}

inline bool is_big_endian()
{
	return not is_little_endian();
}

auto hton(concepts::arithmetic_p auto t)
{
	return is_big_endian() ? t : reverse(t);
}

auto hton(concepts::enumerate_p auto e)
{
	using type = byte_type<sizeof(e)>::unsigned_t;
	using enum_t = std::remove_cvref_t<decltype(e)>;
	return static_cast<enum_t>(hton(static_cast<type>(e)));
}

auto *hton(auto *data, size_t len)
{
	return is_big_endian() ? data : reverse(data, len);
}

auto ntoh(concepts::arithmetic_p auto t)
{
	return is_big_endian() ? t : reverse(t);
}

auto ntoh(concepts::enumerate_p auto e)
{
	using type = byte_type<sizeof(e)>::unsigned_t;
	using enum_t = std::remove_cvref_t<decltype(e)>;
	return static_cast<enum_t>(ntoh(static_cast<type>(e)));
}

auto *ntoh(auto *data, size_t len)
{
	return is_big_endian() ? data : reverse(data, len);
}

auto reverse(concepts::arithmetic_p auto t)
{
	for(size_t i=0; i<sizeof(t)>>1; i++)
	{
		auto m = reinterpret_cast<char*>(&t) + i;
		auto n = reinterpret_cast<char*>(&t) + sizeof(t) - i - 1;
		std::swap(*m, *n);
	}
	return t;
}

auto reverse(concepts::enumerate_p auto e)
{
	using type = byte_type<sizeof(e)>::unsigned_t;
	using enum_t = std::remove_cvref_t<decltype(e)>;
	return static_cast<enum_t>(reverse(static_cast<type>(e)));
}

auto *reverse(auto *data, size_t len)
{
	constexpr auto type_len = sizeof(*data);
	for(size_t i=0; i<len; i++)
	{
		auto array = reinterpret_cast<char*>(data + i);
		for(size_t j=0; j<type_len>>1; j++)
			std::swap(array[j], array[type_len - j - 1]);
	}
	return data;
}

auto to_big_endian(concepts::arithmetic_p auto t)
{
	return is_big_endian() ? t : reverse(t);
}

auto to_big_endian(concepts::enumerate_p auto e)
{
	using type = byte_type<sizeof(e)>::unsigned_t;
	using enum_t = std::remove_cvref_t<decltype(e)>;
	return static_cast<enum_t>(to_big_endian(static_cast<type>(e)));
}

auto *to_big_endian(auto *data, size_t len)
{
	return is_big_endian() ? data : reverse(data, len);
}

auto to_little_endian(concepts::arithmetic_p auto t)
{
	return is_little_endian() ? t : reverse(t);
}

auto to_little_endian(concepts::enumerate_p auto e)
{
	using type = byte_type<sizeof(e)>::unsigned_t;
	using enum_t = std::remove_cvref_t<decltype(e)>;
	return static_cast<enum_t>(to_little_endian(static_cast<type>(e)));
}

auto *to_little_endian(auto *data, size_t len)
{
	return is_little_endian() ? data : reverse(data, len);
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_BYTE_ORDER_H
