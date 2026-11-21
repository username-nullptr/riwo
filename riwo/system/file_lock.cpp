// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/file_lock.h>

#ifdef _WIN32
# include <Windows.h>
#else //_WIN32
# include <cerrno>
# include <unistd.h>
# include <fcntl.h>
# include <sys/file.h>
#endif //_WIN32

namespace riwo::system
{

class RIWO_DECL_HIDDEN file_lock::impl
{
	RIWO_DISABLE_COPY(impl)

public:
	impl(std::intptr_t handle) noexcept :
		m_handle(handle) {}

	impl(impl &&other) noexcept :
		m_handle(std::exchange(other.m_handle, -1)) {}

	impl &operator=(impl &&other) noexcept
	{
		unlock();
		m_handle = std::exchange(other.m_handle, -1);
		return *this;
	}

public:
	[[nodiscard]] bool owns_lock() const noexcept {
		return m_handle != -1;
	}

	void unlock() noexcept
	{
		if( not owns_lock() )
			return ;

		const auto handle = std::exchange(m_handle, -1);
#ifdef _WIN32
		OVERLAPPED range {};
		const auto file = reinterpret_cast<HANDLE>(handle);

		UnlockFileEx(file, 0, 1, 0, &range);
		CloseHandle(file);

#else //_WIN32
		const auto file = static_cast<int>(handle);
		while( ::flock(file, LOCK_UN) < 0 and errno == EINTR ) {}
		::close(file);
#endif //_WIN32
	}

	std::intptr_t m_handle = 0;
};


file_lock::file_lock(std::intptr_t handle) noexcept :
	m_impl(std::make_unique<impl>(handle))
{

}

file_lock::file_lock(file_lock &&other) noexcept :
	m_impl(std::make_unique<impl>(std::move(*other.m_impl)))
{

}

file_lock &file_lock::operator=(file_lock &&other) noexcept
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

file_lock::~file_lock()
{
	unlock();
}

sys_expected<optional<file_lock>> file_lock::try_lock(const std::filesystem::path &path)
{
	using path_t = std::filesystem::path;
	if( path.empty() or path.native().find(path_t::value_type{}) != path_t::string_type::npos )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

#ifdef _WIN32
	const auto handle = CreateFileW (
		path.c_str(), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
	);
	if( handle == INVALID_HANDLE_VALUE )
		return sys_unexpected(error_code(static_cast<int>(GetLastError()), system_category()));

	OVERLAPPED range {};
	if( LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range) )
		return std::optional(file_lock(reinterpret_cast<std::intptr_t>(handle)));

	const DWORD error = GetLastError();
	CloseHandle(handle);

	if( error == ERROR_LOCK_VIOLATION )
		return std::optional<file_lock> {};

	return sys_unexpected(error_code(static_cast<int>(error), system_category()));

#else //_WIN32
	int file = 0;
	do {
		file = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	}
	while( file < 0 and errno == EINTR );

	if( file < 0 )
		return sys_unexpected(error_code(errno, riwo::system_category()));

	int result;
	do {
		result = ::flock(file, LOCK_EX | LOCK_NB);
	}
	while( result < 0 and errno == EINTR );

	if( result == 0 )
		return std::optional(file_lock(file));

	const int error = errno;
	::close(file);

	if( error == EWOULDBLOCK )
		return std::optional<file_lock> {};

	return sys_unexpected(error_code(error, riwo::system_category()));
#endif //_WIN32
}

bool file_lock::owns_lock() const noexcept
{
	return m_impl->owns_lock();
}

void file_lock::unlock() noexcept
{
	m_impl->unlock();
}

} //namespace riwo::system
