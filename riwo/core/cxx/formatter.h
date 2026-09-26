// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_FORMATTER_H
#define RIWO_CORE_CXX_FORMATTER_H

#include <riwo/core/cxx/string_concepts.h>
#include <riwo/core/cxx/attributes.h>
#include <algorithm>
#include <format>

namespace riwo
{

template <concepts::character CharT>
struct RIWO_CORE_TAPI no_parse_formatter
{
	constexpr auto parse(std::basic_format_parse_context<CharT> &context) noexcept {
		return std::ranges::find(context, static_cast<CharT>(0x7D));
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_FORMATTER_H
