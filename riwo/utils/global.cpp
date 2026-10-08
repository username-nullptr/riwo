// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "global.h"

namespace riwo::utils
{

asio::thread_pool &thread_pool() noexcept
{
	static asio::thread_pool pool (
		std::thread::hardware_concurrency()
	);
	return pool;
}

} //namespace riwo::utils
