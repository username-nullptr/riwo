// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "secure_random.h"

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <bcrypt.h>
#elif defined(__linux__) && !defined(__ANDROID__)
# include <sys/random.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
	defined(__OpenBSD__) || defined(__DragonFly__)
# include <cstdlib>
#else
# include <fcntl.h>
# include <unistd.h>
#endif

namespace riwo::websocket::detail { namespace
{

[[nodiscard]] sys_expected<> invalid_output() noexcept
{
	return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
}

#if !defined(_WIN32) && \
	!(defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
	  defined(__OpenBSD__) || defined(__DragonFly__))
[[nodiscard]] error_code errno_error() noexcept {
	return { std::error_code(errno, std::generic_category()) };
}
#endif //...

} //namespace

sys_expected<> secure_random_bytes(const mutable_buffer &output) noexcept
{
	if( output.size() == 0 )
		return make_sys_expected();

	if( output.data() == nullptr )
		return invalid_output();

	auto *data = static_cast<std::byte*>(output.data());
	auto remaining = output.size();

#if defined(_WIN32)
	while( remaining > 0 )
	{
		auto chunk = static_cast<ULONG>(std::min<size_t>(
			remaining, std::numeric_limits<ULONG>::max()
		));
		auto status = BCryptGenRandom (
			nullptr, reinterpret_cast<PUCHAR>(data), chunk,
			BCRYPT_USE_SYSTEM_PREFERRED_RNG
		);
		if( status < 0 )
		{
			return sys_unexpected(error_code (
				static_cast<int>(status), system_category()
			));
		}
		data += chunk;
		remaining -= chunk;
	}
#elif defined(__linux__) && !defined(__ANDROID__)
	while( remaining > 0 )
	{
		// Linux guarantees that urandom requests no larger than 256 bytes are
		// completed atomically once the entropy pool has initialized.
		auto chunk = std::min<size_t>(remaining, 256);
		auto size = ::getrandom(data, chunk, 0);
		if( size < 0 )
		{
			if( errno == EINTR )
				continue;
			return sys_unexpected(errno_error());
		}
		if( size == 0 )
		{
			return sys_unexpected (
				make_system_error_code(std::errc::io_error)
			);
		}
		data += static_cast<size_t>(size);
		remaining -= static_cast<size_t>(size);
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
	defined(__OpenBSD__) || defined(__DragonFly__)
	::arc4random_buf(data, remaining);
#else
	int flags = O_RDONLY;

# if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
# endif
	int descriptor = -1;
	do {
		descriptor = ::open("/dev/urandom", flags);
	}
	while( descriptor < 0 and errno == EINTR );

	if( descriptor < 0 )
		return sys_unexpected(errno_error());

	error_code read_error {};
	while( remaining > 0 )
	{
		auto chunk = std::min<size_t>(
			remaining, static_cast<size_t>(std::numeric_limits<ssize_t>::max())
		);
		auto size = ::read(descriptor, data, chunk);
		if( size < 0 )
		{
			if( errno == EINTR )
				continue;
			read_error = errno_error();
			break;
		}
		if( size == 0 )
		{
			read_error = make_system_error_code(std::errc::io_error);
			break;
		}
		data += static_cast<size_t>(size);
		remaining -= static_cast<size_t>(size);
	}
	ignore_unused(::close(descriptor));
	if( read_error )
		return sys_unexpected(read_error);
#endif
	return make_sys_expected();
}

} //namespace riwo::websocket::detail
