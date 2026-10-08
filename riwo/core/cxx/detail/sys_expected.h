// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_SYS_EXPECTED_H
#define RIWO_CORE_CXX_DETAIL_SYS_EXPECTED_H

namespace riwo
{

template <concepts::optional_value_p Value>
auto make_sys_expected(Value &&value)
{
	return make_expected<error_code>(std::forward<Value>(value));
}

inline sys_expected<> make_sys_expected()
{
	return make_expected<error_code>();
}

template <concepts::expected_value Value>
void sys_expected_loc_throw(const sys_expected<Value> &expected, std::source_location loc)
{
	if( not expected )
		system_error::loc_throw(expected.error(), std::move(loc));
}

template <concepts::expected_value Value>
void sys_expected_loc_throw
(const sys_expected<Value> &expected, concepts::text_p<char> auto &&msg, std::source_location loc)
{
	if( not expected )
		system_error::loc_throw(expected.error(), strtls::to_view(msg), std::move(loc));
}

inline io_expected make_io_expected(size_t sum)
{
	return make_sys_expected(sum);
}

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_SYS_EXPECTED_H
