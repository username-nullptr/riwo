// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "expected.h"
#if !RIWO_HAS_STD_EXPECTED

namespace riwo
{

const char *bad_expected_access<void>::what() const noexcept
{
	return "bad access to riwo::expected without an expected value";
}

} //namespace riwo

#endif //RIWO_HAS_STD_EXPECTED
