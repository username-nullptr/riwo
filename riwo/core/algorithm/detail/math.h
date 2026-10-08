// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_DETAIL_MATH_H
#define RIWO_CORE_ALGORITHM_DETAIL_MATH_H

namespace riwo
{

template <typename Iter>
auto mean(Iter begin, Iter end) requires
	concepts::arithmetic_p<decltype(*begin)>
{
	return mean(begin, end, [](auto &x){return &x;});
}

template <typename Iter, typename Func>
auto mean(Iter begin, Iter end, Func &&func) requires
(concepts::mean_value_projection<Iter,Func> or concepts::mean_iterator_projection<Iter,Func>)
{
	auto project = [&func](Iter it) -> decltype(auto)
	{
		if constexpr( concepts::mean_value_projection<Iter,Func> )
			return func(*it);
		else
			return func(it);
	};
	using sum_t = std::remove_cvref_t<decltype(*project(begin))>;

	auto sum = static_cast<sum_t>(0);
	auto count = static_cast<sum_t>(0);

	for(auto it=begin; it!=end; ++it)
	{
		auto p = project(it);
		using p_t = std::remove_cvref_t<decltype(p)>;

		if constexpr( std::is_pointer_v<p_t> )
		{
			if( not p )
				continue;
		}
		sum += *p;
		++count;
	}
	return sum / count;
}

} //namespace riwo


#endif //RIWO_CORE_ALGORITHM_DETAIL_MATH_H
