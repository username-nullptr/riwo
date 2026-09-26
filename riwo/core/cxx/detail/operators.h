// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_OPERATOR_H
#define RIWO_CORE_CXX_DETAIL_OPERATOR_H

namespace riwo
{

constexpr bool equality(concepts::arithmetic_p auto a, concepts::arithmetic_p auto b)
{
	if constexpr( is_float_v<decltype(a)> or is_float_v<decltype(b)> )
	{
		auto e = std::abs(a - b) ;
		return e < std::numeric_limits<decltype(e)>::epsilon();
	}
	else
		return a == b;
}

constexpr bool nequality(concepts::arithmetic_p auto a, concepts::arithmetic_p auto b)
{
	return not equality(a,b);
}

constexpr bool equal_greater(concepts::arithmetic_p auto a, concepts::arithmetic_p auto b)
{
	return a > b or equality(a,b);
}

constexpr bool equal_less(concepts::arithmetic_p auto a, concepts::arithmetic_p auto b)
{
	return a < b or equality(a,b);
}

} //namespace riwo

[[nodiscard]] constexpr bool operator==
(riwo::concepts::arithmetic_p auto a, riwo::concepts::arithmetic_p auto b)
{
	if constexpr( riwo::is_float_v<decltype(a)> or riwo::is_float_v<decltype(b)> )
	{
		auto e = std::abs(a - b) ;
		return e < std::numeric_limits<decltype(e)>::epsilon();
	}
	else
		return a == b;
}

[[nodiscard]] constexpr bool operator!=
(riwo::concepts::arithmetic_p auto a, riwo::concepts::arithmetic_p auto b)
{
	return not riwo::equality(a,b);
}

[[nodiscard]] constexpr bool operator>=
(riwo::concepts::arithmetic_p auto a, riwo::concepts::arithmetic_p auto b)
{
	return a > b or riwo::equality(a,b);
}

[[nodiscard]] constexpr bool operator<=
(riwo::concepts::arithmetic_p auto a, riwo::concepts::arithmetic_p auto b)
{
	return a < b or riwo::equality(a,b);
}


#endif //RIWO_CORE_CXX_DETAIL_OPERATOR_H
