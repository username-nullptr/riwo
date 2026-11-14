// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__unix__) || defined(__APPLE__)

#include <riwo/core/executable.h>

#include <cerrno>
#include <unistd.h>

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

namespace riwo::executable
{

[[nodiscard]] static error_code sys_error(int value = errno)
{
	return error_code(std::error_code(value, std::system_category()));
}

sys_expected<path_type> path() noexcept
{
#ifdef __APPLE__
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::vector<char> buffer(size);
	if( _NSGetExecutablePath(buffer.data(), &size) != 0 )
		return sys_error(EINVAL);
	return path_type(buffer.data());
#else
	std::vector<char> buffer(1024);
	for(;;)
	{
		const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size());
		if( size < 0 )
			return sys_error();
		if( static_cast<size_t>(size) < buffer.size() )
			return path_type(std::string(buffer.data(), static_cast<size_t>(size)));
		buffer.resize(buffer.size() * 2);
	}
#endif
}

} //namespace riwo::executable

#endif //__unix__ || __APPLE__
