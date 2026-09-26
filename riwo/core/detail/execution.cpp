// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/execution.h>

namespace riwo::detail
{

void local_dispatch_state::finish() noexcept
{
	m_finished.store(true, std::memory_order_release);
	m_finished.notify_all();
}

bool local_dispatch_state::finished() const noexcept
{
	return m_finished.load(std::memory_order_acquire);
}

void local_dispatch_state::wait() const noexcept
{
	m_finished.wait(false, std::memory_order_acquire);
}

} //namespace riwo::detail
