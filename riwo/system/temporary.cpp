// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/temporary.h>

#ifdef _WIN32
# include <objbase.h>
#else //_WIN32
# include <cerrno>
# include <cstdlib>
# include <unistd.h>
#endif //_WIN32

namespace riwo::system::detail
{

sys_expected<std::filesystem::path>
create_temporary_path(const std::filesystem::path &parent, bool directory)
{
	namespace fs = std::filesystem;

	std::filesystem::path directory_path {};
	error_code error {};

	if( parent.empty() )
	{
		directory_path = fs::temp_directory_path(error);
		if( error )
			return sys_unexpected(error);
	}
	else
		directory_path = parent;

	const auto root = fs::absolute(directory_path, error);
	if( error )
		return sys_unexpected(error);

	if( root.native().find(fs::path::value_type{}) != fs::path::string_type::npos )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

#ifdef _WIN32
	for( int attempt = 0; attempt < 16; ++attempt )
	{
		GUID id {};
		if( const auto result = CoCreateGuid(&id); FAILED(result) )
			return sys_unexpected(error_code(static_cast<int>(HRESULT_CODE(result)), system_category()));

		wchar_t name[39] {};
		StringFromGUID2(id, name, std::size(name));

		auto path = root / (std::wstring(L"riwo-") + name);
		if( directory )
		{
			if( CreateDirectoryW(path.c_str(), nullptr) )
				return path;
		}
		else
		{
			const auto handle = CreateFileW (
				path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
				nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
			);
			if( handle != INVALID_HANDLE_VALUE )
			{
				CloseHandle(handle);
				return path;
			}
		}
		const auto reason = GetLastError();
		if( reason != ERROR_ALREADY_EXISTS and reason != ERROR_FILE_EXISTS )
			return sys_unexpected(error_code(static_cast<int>(reason), system_category()));
	}
	return sys_unexpected(make_system_error_code(std::errc::file_exists));

#else //_WIN32
	auto name = (root / "riwo-XXXXXXXXXXXX").native();
	if( directory )
	{
		if( not ::mkdtemp(name.data()) )
			return sys_unexpected(error_code(errno, riwo::system_category()));
	}
	else
	{
#ifdef __linux__
		const int file = ::mkostemp(name.data(), O_CLOEXEC);
#else //__linux__
		const int file = ::mkstemp(name.data());
#endif //__linux__

		if( file < 0 )
			return sys_unexpected(error_code(errno, riwo::system_category()));
		::close(file);
	}
	return fs::path(std::move(name));
#endif //_WIN32
}

} //namespace riwo::system::detail
