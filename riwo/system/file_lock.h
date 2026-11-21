// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_FILE_LOCK_H
#define RIWO_SYSTEM_FILE_LOCK_H

#include <riwo/system/global.h>
#include <riwo/core/sys_expected.h>
#include <optional>

namespace riwo::system
{

class RIWO_SYSTEM_API file_lock
{
	RIWO_DISABLE_COPY(file_lock)

public:
	file_lock(file_lock &&other) noexcept;
	file_lock &operator=(file_lock &&other) noexcept;
	~file_lock();

	// An empty optional means another owner holds the lock. The file persists
	// after unlock, so processes opening the same path synchronize with each other.
	[[nodiscard]] static expected<optional<file_lock>,error_code>
	try_lock(const std::filesystem::path &path);

	[[nodiscard]] bool owns_lock() const noexcept;
	void unlock() noexcept;

private:
	explicit file_lock(std::intptr_t handle) noexcept;

	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::system


#endif //RIWO_SYSTEM_FILE_LOCK_H
