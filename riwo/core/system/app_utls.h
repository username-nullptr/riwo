// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_APP_UTILS_H
#define RIWO_CORE_SYSTEM_APP_UTILS_H

#include <riwo/core/value.h>
#include <map>

namespace riwo::app
{

using path_t = std::filesystem::path;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> file_path() noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> dir_path() noexcept;

/*[[nodiscard]]*/ RIWO_CORE_API
sys_expected<> set_current_directory(const path_t &path) noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> current_directory() noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> absolute_path(const path_t &path) noexcept;

[[nodiscard]] RIWO_CORE_API
bool is_absolute_path(const path_t &path) noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<std::string> getenv(std::string_view key) noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<std::map<std::string,std::string>> getenvs() noexcept;

/*[[nodiscard]]*/ RIWO_CORE_API
sys_expected<> setenv(std::string_view key, const riwo::value &value, bool overwrite = true) noexcept;

/*[[nodiscard]]*/ RIWO_CORE_API
sys_expected<> unsetenv(std::string_view key) noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<std::string> current_user() noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> home_directory() noexcept;

} //namespace riwo::app
#include <riwo/core/system/detail/app_utls.h>


#endif //RIWO_CORE_SYSTEM_APP_UTILS_H
