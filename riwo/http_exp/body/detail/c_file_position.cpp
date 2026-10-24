// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "c_file_position.h"
#include <cerrno>
#include <limits>

#ifndef _WIN32
# include <sys/types.h>
#endif

namespace riwo::http_exp::detail
{

int c_file_seek(std::FILE *file, int64_t offset, int origin) noexcept
{
#ifdef _WIN32
	return ::_fseeki64(file, offset, origin);
#else //_WIN32
	if constexpr( sizeof(off_t) < sizeof(int64_t) )
	{
		if( offset < static_cast<int64_t>(std::numeric_limits<off_t>::min()) or
			offset > static_cast<int64_t>(std::numeric_limits<off_t>::max()) )
		{
			errno = EOVERFLOW;
			return -1;
		}
	}
	return ::fseeko(file, static_cast<off_t>(offset), origin);
#endif //_WIN32
}

int64_t c_file_tell(std::FILE *file) noexcept
{
#ifdef _WIN32
	return ::_ftelli64(file);
#else //_WIN32
	const auto value = ::ftello(file);
	if( value < 0 )
		return -1;

	if constexpr( sizeof(off_t) > sizeof(int64_t) )
	{
		if( value > static_cast<off_t>(std::numeric_limits<int64_t>::max()) )
		{
			errno = EOVERFLOW;
			return -1;
		}
	}
	return static_cast<int64_t>(value);
#endif //_WIN32
}

} //namespace riwo::http_exp::detail
