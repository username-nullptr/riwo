// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_STRING_LIST_H
#define RIWO_CORE_STRING_LIST_H

#include <riwo/core/string_container.h>
#include <list>

namespace riwo
{

template <concepts::character CharT, typename...Args>
using basic_string_list = basic_string_container<CharT,std::list,Args...>;

using string_list    = basic_string_list<char    >;
using wstring_list   = basic_string_list<wchar_t >;
using u8string_list  = basic_string_list<char8_t >;
using u16string_list = basic_string_list<char16_t>;
using u32string_list = basic_string_list<char32_t>;

} //namespace riwo


#endif //RIWO_CORE_STRING_LIST_H
