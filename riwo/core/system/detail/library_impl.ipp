// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_DETAIL_LIBRARY_IMPL_HII
#define RIWO_CORE_SYSTEM_DETAIL_LIBRARY_IMPL_HII

#include <riwo/core/system/library.h>

#if defined(__WINNT__) || defined(_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#endif //Windows

namespace riwo
{

class RIWO_DECL_HIDDEN library::impl
{
	RIWO_DISABLE_COPY(impl)

	using handle_t =
#if defined(__WINNT__) || defined(_WINDOWS)
	HMODULE
#else // Unix/Linux
	void*
#endif //OS
	;

public:
	impl(path_t file_name, std::string_view version)
#ifdef __linux__
		: m_version(version) {
#else
	{
		ignore_unused(version);
#endif //__linux__
		set_file_name(std::move(file_name));
	}

	impl(impl &&other) noexcept :
		m_file_name(std::move(other.m_file_name)),
		m_load_count(other.m_load_count.exchange(0)),
		m_handle(std::exchange(other.m_handle, nullptr))
#ifdef __linux__
		, m_version(std::move(other.m_version))
#endif //__linux__
	{}
	impl &operator=(impl &&other) noexcept
	{
		if( this == &other )
			return *this;
		if( m_handle )
			RIWO_UNUSED(unload_native());
		m_file_name = std::move(other.m_file_name);
		m_load_count = other.m_load_count.exchange(0);
		m_handle = std::exchange(other.m_handle, nullptr);
#ifdef __linux__
		m_version = std::move(other.m_version);
#endif //__linux__
		return *this;
	}
	~impl()
	{
		if( m_handle )
			RIWO_UNUSED(unload_native());
	}

public:
	[[nodiscard]] error_code load()
	{
		error_code error;
		if( m_handle )
		{
			++m_load_count;
			return error;
		}
		error = load_native();
		if( not error )
			++m_load_count;
		return error;
	}

	[[nodiscard]] error_code unload()
	{
		error_code error;
		if( not m_handle )
			return error;
		--m_load_count;

		if( not m_load_count )
		{
			error = unload_native();
			if( not error )
				m_handle = nullptr;
		}
		return error;
	}

public:
	[[nodiscard]] void *interface(std::string_view ifname) const;
	[[nodiscard]] bool exists(std::string_view ifname) const;

private:
	void set_file_name(path_t file_name);
	[[nodiscard]] error_code load_native();
	[[nodiscard]] error_code unload_native();

public:
	path_t m_file_name;
	std::atomic_int m_load_count {0};
	handle_t m_handle = nullptr;

#ifdef __linux__
	std::string m_version;
#endif //__linux__
};

} //namespace riwo


#endif //RIWO_CORE_SYSTEM_DETAIL_LIBRARY_IMPL_HII
