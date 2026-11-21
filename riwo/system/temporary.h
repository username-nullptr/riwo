// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_TEMPORARY_H
#define RIWO_SYSTEM_TEMPORARY_H

#include <riwo/system/global.h>
#include <riwo/core/sys_expected.h>

namespace riwo::system
{

template <bool Directory>
class basic_temp_path
{
	RIWO_DISABLE_COPY(basic_temp_path)

public:
	basic_temp_path(basic_temp_path &&other) noexcept;
	basic_temp_path &operator=(basic_temp_path &&other) noexcept;
	~basic_temp_path();

public:
	[[nodiscard]] static expected<basic_temp_path,error_code>
	create(const std::filesystem::path &parent = {});

	// Keep the resource and transfer its path to the caller.
	[[nodiscard]] std::filesystem::path release() noexcept;
	[[nodiscard]] sys_expected<> remove();

	[[nodiscard]] const std::filesystem::path &path() const noexcept;

private:
	explicit basic_temp_path(std::filesystem::path path) noexcept;
	std::filesystem::path m_path {};
};

using temp_file = basic_temp_path<false>;
using temp_directory = basic_temp_path<true>;

} //namespace riwo::system
#include <riwo/system/detail/temporary.h>


#endif //RIWO_SYSTEM_TEMPORARY_H
