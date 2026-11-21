// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/paths.h>
#include <riwo/system/environment.h>
#include <riwo/system/user.h>

#ifdef _WIN32
# include <knownfolders.h>
# include <shlobj.h>
#endif

namespace riwo::system { namespace
{

namespace fs = std::filesystem;

[[nodiscard]] bool valid_app_id(std::string_view id) noexcept
{
	const auto alphanumeric = [](char c) {
		return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or (c >= '0' and c <= '9');
	};
	if( id.empty() or not alphanumeric(id.front()) or id.back() == '.' or
		not std::ranges::all_of(id, [alphanumeric](char c) {
			return alphanumeric(c) or c == '_' or c == '-' or c == '.';
		}) )
		return false;

	const auto prefix = id.substr(0, id.find('.'));
	if( prefix.size() != 3 and prefix.size() != 4 )
		return true;

	const auto lower = [](char c) {
		return c >= 'A' and c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
	};
	const std::array characters {
		lower(prefix[0]), lower(prefix[1]), lower(prefix[2]),
		prefix.size() == 4 ? lower(prefix[3]) : '\0'
	};
	const std::string_view name(characters.data(), prefix.size());

	return name != "con" and name != "prn" and name != "aux" and name != "nul" and not (
		(name.starts_with("com") or name.starts_with("lpt")) and
		name.size() == 4 and name.back() >= '0' and name.back() <= '9'
	);
}

#ifdef _WIN32
[[nodiscard]] sys_expected<fs::path> known_folder(REFKNOWNFOLDERID id)
{
	PWSTR value = nullptr;
	const auto result = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &value);
	const std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> memory(value, &CoTaskMemFree);

	if( FAILED(result) )
		return sys_unexpected(error_code(static_cast<int>(HRESULT_CODE(result)), system_category()));

	if( not value or not *value )
		return sys_unexpected(make_system_error_code(std::errc::no_such_file_or_directory));
	return fs::path(value);
}
#else //_WIN32
[[nodiscard]] sys_expected<fs::path> absolute_env_path(std::string_view name)
{
	const auto value = environment::get(name);
	if( not value )
		return sys_unexpected(value.error());

	auto path = *value ? fs::path(**value) : fs::path {};
	return path.is_absolute() ? std::move(path) : fs::path {};
}
#endif //_WIN32

} //namespace

sys_expected<application_paths> app_paths(std::string_view app_id)
{
	if( not valid_app_id(app_id) )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

	const fs::path id(app_id);

#ifdef _WIN32
	const auto roaming = known_folder(FOLDERID_RoamingAppData);
	if( not roaming )
		return sys_unexpected(roaming.error());

	const auto local = known_folder(FOLDERID_LocalAppData);
	if( not local )
		return sys_unexpected(local.error());

	const auto base = *local / id;
	return application_paths {
		.config = *roaming / id / "config",
		.data = base / "data",
		.cache = base / "cache",
		.state = base / "state"
	};
#else //_WIN32
	application_paths result {};

	const std::array members {
		&result.config, &result.data, &result.cache, &result.state
	};
	fs::path home {};

	for(size_t i=0; i<members.size(); i++)
	{
		constexpr std::array variables {
			"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"
		};
		auto root = absolute_env_path(variables[i]);
		if( not root )
			return sys_unexpected(root.error());

		if( root->empty() )
		{
			constexpr std::array defaults {
				".config", ".local/share", ".cache", ".local/state"
			};
			if( home.empty() )
			{
				auto value = absolute_env_path("HOME");
				if( value and value->empty() )
					value = home_directory();

				if( not value )
					return sys_unexpected(value.error());

				home = std::move(*value);
				if( not home.is_absolute() )
					return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
			}
			*root = home / defaults[i];
		}
		*members[i] = *root / id;
	}
	return result;
#endif //_WIN32
}

} //namespace riwo::system
