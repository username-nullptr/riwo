// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_STRING_DEQUE_H
#define RIWO_CORE_STRING_DEQUE_H

#include <riwo/core/string_container.h>
#include <vector>

namespace riwo
{

template <concepts::character CharT, typename...Args>
using basic_string_vector = basic_string_container<CharT,std::vector,Args...>;

using string_vector    = basic_string_vector<char    >;
using wstring_vector   = basic_string_vector<wchar_t >;
using u8string_vector  = basic_string_vector<char8_t >;
using u16string_vector = basic_string_vector<char16_t>;
using u32string_vector = basic_string_vector<char32_t>;

} //namespace riwo


#endif //RIWO_CORE_STRING_DEQUE_H
