// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_SETTINGS_H
#define RIWO_UTILS_SETTINGS_H

#include <riwo/utils/signal_slot.h>
#include <riwo/core/ini.h>

namespace riwo::utils
{

class RIWO_UTILS_API settings
{
	RIWO_DISABLE_COPY_MOVE(settings)
	explicit settings(std::string name);
	~settings();

public:
	using ini_t = riwo::ini;
	using path_t = ini_t::path_t;
	using group_key_t = ini_t::group_key;

	[[nodiscard]] static settings &instance(std::string_view name, bool create = true);
	[[nodiscard]] static settings &instance();

	[[nodiscard]] static std::vector<std::string> names() noexcept;
	[[nodiscard]] path_t file_name() const noexcept;

public:
	[[nodiscard]] optional<value> get(const group_key_t &gk);
	[[nodiscard]] optional<value> get(concepts::string_p<char> auto &&path);

	settings &set (
		const group_key_t &gk,
		const concepts::value_set<char> auto &value
	) noexcept;

	settings &set (
		const concepts::string_p<char> auto &path,
		const concepts::value_set<char> auto &value
	) noexcept;

public:
	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto load(const path_t &file_name, Token &&token = {});

	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto load_or(const path_t &file_name, Token &&token = {});

	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto load(Token &&token = {});

	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto load_or(Token &&token = {});

	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto sync(const path_t &file_name, Token &&token = {});

	template <concepts::opt_token<error_code> Token = use_sync_t>
	auto sync(Token &&token = {});

public:
	signal<void(std::string_view,value)> changed;
	signal<void(error_code)> loaded;
	signal<void(error_code)> synced;

public:
	[[nodiscard]] std::string_view name() const noexcept;
	[[nodiscard]] const ini_t &ini() const noexcept;
	[[nodiscard]] ini_t &ini() noexcept;

private:
	class impl;
	impl *m_impl = nullptr;
};

} //namespace riwo::utils
#include <riwo/utils/detail/settings.h>


#endif //RIWO_UTILS_SETTINGS_H
