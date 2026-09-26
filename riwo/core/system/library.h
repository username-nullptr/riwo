// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_LIBRARY_H
#define RIWO_CORE_SYSTEM_LIBRARY_H

#include <riwo/core/global.h>

namespace riwo
{

class RIWO_CORE_API library
{
	RIWO_DISABLE_COPY(library)

public:
	using path_t = std::filesystem::path;

	explicit library(path_t file_name, std::string_view version = {});
	~library();

	library(library &&other) noexcept;
	library &operator=(library &&other) noexcept;

public:
	sys_expected<> load() noexcept;
	sys_expected<> unload() noexcept;

public:
	template <concepts::function Func>
	[[nodiscard]] auto interface(std::string_view if_name) const;

	template <concepts::function Func, typename Arg0, typename...Args>
	[[nodiscard]] auto interface (
		std::format_string<Arg0,Args...> fmt_value, Arg0 &&arg0, Args&&...args
	) const;

	[[nodiscard]] optional<void*> interface (
		std::string_view if_name
	) const;

public:
	[[nodiscard]] bool exists(std::string_view if_name) const;
	[[nodiscard]] bool is_loaded() const noexcept;
	[[nodiscard]] path_t file_name() const noexcept;

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo
#include <riwo/core/system/detail/library.h>


#endif //RIWO_CORE_SYSTEM_LIBRARY_H
