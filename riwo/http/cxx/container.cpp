// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "container.h"

namespace riwo::http
{

bool less_case_insensitive::operator()(const key_t &v1, const key_t &v2) const
{
	return std::ranges::lexicographical_compare (v1, v2,
		[](char c1, char c2) {
			return strtls::to_lower(c1) < strtls::to_lower(c2);
		});
}

} //namespace riwo::http
