// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_PATHS_H
#define RIWO_SYSTEM_PATHS_H

#include <riwo/system/global.h>
#include <riwo/core/sys_expected.h>

namespace riwo::system
{

struct application_paths
{
	std::filesystem::path config {};
	std::filesystem::path data {};
	std::filesystem::path cache {};
	std::filesystem::path state {};
};

// Resolve per-user paths for a portable application identifier. Creates no directories.
[[nodiscard]] RIWO_SYSTEM_API
sys_expected<application_paths> app_paths(std::string_view app_id);

} //namespace riwo::system


#endif //RIWO_SYSTEM_PATHS_H
