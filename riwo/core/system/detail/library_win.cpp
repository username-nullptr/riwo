// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#include "library_impl.ipp"
#include <riwo/core/system/app_utls.h>

namespace fs = std::filesystem;

namespace riwo
{

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
// Boost.System deliberately gives error_category a protected non-virtual
// destructor. This static final category is never deleted polymorphically, but
// GCC still diagnoses the supported inheritance pattern under
// -Wnon-virtual-dtor.
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif //BOOST & GNU

static class RIWO_DECL_HIDDEN library_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(library_category)

public:
	library_category() = default;
#if !RIWO_USING_BOOST_ASIO
	~library_category() override = default;
#endif //RIWO_USING_BOOST_ASIO

public:
	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::library_error_category";
	}
	[[nodiscard]] std::string message(int code) const override
	{
		std::string result;
		switch(code)
		{
		case 0     : result = "Success"                  ; break;
		case EACCES: result = "Permission denied"        ; break;
		case EMFILE: result = "Too many open files"      ; break;
		case ENOENT: result = "No such file or directory"; break;
		case ENOSPC: result = "No space left on device"  ; break;
		default: {
			char error_str[1024] {0};
			const auto buffer_size = FormatMessage (
				FORMAT_MESSAGE_FROM_SYSTEM, nullptr, static_cast<DWORD>(code),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&error_str),
				sizeof(error_str), nullptr
			);
			result = std::string(error_str, buffer_size);
			break;}
		}
		return result;
	}
}
g_library_category;

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif //BOOST & GNU

void *library::impl::interface(std::string_view ifname) const
{
	return reinterpret_cast<void*>(GetProcAddress(m_handle, ifname.data()));
}

bool library::impl::exists(std::string_view ifname) const
{
	return reinterpret_cast<void*>(GetProcAddress(m_handle, ifname.data())) != nullptr;
}

void library::impl::set_file_name(fs::path file_name)
{
	m_file_name = std::move(file_name);
	auto _file_name = m_file_name.wstring();
	std::vector candidates
	{
		_file_name,
		_file_name + L".dll",
	};
	if( _file_name.find(L'/') == std::wstring::npos )
	{
		candidates.emplace_back(L"lib" + _file_name);
		candidates.emplace_back(L"lib" + _file_name + L".dll");
	};
	for(auto &name : candidates)
	{
		auto abs_name = *app::absolute_path(name).or_else();
		if( not fs::exists(abs_name) )
			continue;

		m_file_name = std::move(abs_name);
		break;
	}
}

error_code library::impl::load_native()
{
	if( not fs::exists(m_file_name) )
		return std::make_error_code(std::errc::no_such_file_or_directory);

	auto _file_name = m_file_name.wstring();
	m_handle = LoadLibraryW(_file_name.c_str());

	if( not m_handle )
		return {static_cast<int>(GetLastError()), g_library_category};
	return {};
}

error_code library::impl::unload_native()
{
	if( not FreeLibrary(m_handle) )
		return {static_cast<int>(GetLastError()), g_library_category};
	return {};
}

} //namespace riwo

#endif //Windows
