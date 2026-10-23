// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H
#define RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>

#ifndef _WIN32
# include <sys/types.h>
#endif

namespace riwo::http_exp::detail
{

[[nodiscard]] inline int c_file_seek(
	std::FILE *file, int64_t offset, int origin) noexcept
{
#ifdef _WIN32
	return ::_fseeki64(file, offset, origin);
#else
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
#endif
}

[[nodiscard]] inline int64_t c_file_tell(std::FILE *file) noexcept
{
#ifdef _WIN32
	return ::_ftelli64(file);
#else
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
#endif
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_BODY_DETAIL_C_FILE_POSITION_H
