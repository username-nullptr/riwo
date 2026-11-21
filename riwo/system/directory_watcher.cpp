// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/directory_watcher.h>

#ifdef _WIN32
# include <Windows.h>
#else //_WIN32
# include <cerrno>
# include <sys/stat.h>
#endif //_WIN32

namespace riwo::system::detail { namespace
{

namespace fs = std::filesystem;

#ifdef _WIN32

[[nodiscard]] std::uintmax_t combine(DWORD high, DWORD low) noexcept
{
	return (static_cast<uint64_t>(high) << 32) | low;
}

[[nodiscard]] sys_expected<entry_metadata_t> read_entry(const fs::path &path)
{
	const auto handle = CreateFileW(path.c_str(), 0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr
	);
	if( handle == INVALID_HANDLE_VALUE )
		return sys_unexpected(error_code(static_cast<int>(GetLastError()), system_category()));

	BY_HANDLE_FILE_INFORMATION metadata {};
	const BOOL success = GetFileInformationByHandle(handle, &metadata);

	const auto reason = success ? ERROR_SUCCESS : GetLastError();
	CloseHandle(handle);

	if( not success )
		return sys_unexpected(error_code(static_cast<int>(reason), system_category()));

	auto time = [](const FILETIME &value) {
		return combine(value.dwHighDateTime, value.dwLowDateTime);
	};
	return entry_metadata_t {
		metadata.dwVolumeSerialNumber, combine(metadata.nFileIndexHigh, metadata.nFileIndexLow),
		combine(metadata.nFileSizeHigh, metadata.nFileSizeLow), metadata.dwFileAttributes,
		time(metadata.ftLastWriteTime), time(metadata.ftCreationTime)
	};
}

#else //_WIN32

[[nodiscard]] sys_expected<entry_metadata_t> read_entry(const fs::path &path)
{
	struct stat metadata {};
	if( ::lstat(path.c_str(), &metadata) < 0 )
		return sys_unexpected(error_code(errno, riwo::system_category()));

#ifdef __APPLE__
	const auto &modified = metadata.st_mtimespec;
	const auto &changed = metadata.st_ctimespec;
#else //__APPLE__
	const auto &modified = metadata.st_mtim;
	const auto &changed = metadata.st_ctim;
#endif //__APPLE__

	return entry_metadata_t {
		metadata.st_dev, metadata.st_ino,
		static_cast<std::uintmax_t>(metadata.st_size), static_cast<std::uintmax_t>(metadata.st_mode),
		static_cast<std::uintmax_t>(modified.tv_sec), static_cast<std::uintmax_t>(modified.tv_nsec),
		static_cast<std::uintmax_t>(changed.tv_sec), static_cast<std::uintmax_t>(changed.tv_nsec)
	};
}

#endif //_WIN32

} //namespace

sys_expected<directory_snapshot_t> scan_directory(const fs::path &directory)
{
	directory_snapshot_t snapshot {};
	error_code error {};
	fs::directory_iterator entry(directory, error);

	for(; not error and entry != fs::directory_iterator{}; entry.increment(error))
	{
		const auto &path = entry->path();

		if( auto metadata = read_entry(path) )
			snapshot.emplace(path, *metadata);

		else if( metadata.error() != std::errc::no_such_file_or_directory )
			return sys_unexpected(metadata.error());
	}
	if( error )
		return sys_unexpected(error);
	return snapshot;
}

std::vector<directory_change> compare_directory_snapshots
(const directory_snapshot_t &previous, const directory_snapshot_t &current)
{
	std::vector<directory_change> changes {};
	for(const auto &[path, metadata] : previous)
	{
		if( const auto entry = current.find(path); entry == current.end() )
			changes.push_back({.kind = directory_change_kind::removed, .path = path});

		else if( entry->second != metadata )
			changes.push_back({.kind = directory_change_kind::modified, .path = path});
	}
	for(const auto &path: current | std::views::keys)
	{
		if( not previous.contains(path) )
			changes.push_back({directory_change_kind::created, path});
	}
	return changes;
}

} //namespace riwo::system::detail
