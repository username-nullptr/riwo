// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifdef __unix__

#include "library_impl.ipp"
#include <riwo/core/system/app_utls.h>
#include <dlfcn.h>

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
	[[nodiscard]] std::string message(int) const override
	{
		auto err = dlerror();
		return err ? err : "";
	}
}
g_library_category;

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif //BOOST & GNU

void *library::impl::interface(std::string_view ifname) const
{
	return dlsym(m_handle, ifname.data());
}

bool library::impl::exists(std::string_view ifname) const
{
	return dlsym(m_handle, ifname.data()) != nullptr;
}

void library::impl::set_file_name(fs::path file_name)
{
	m_file_name = std::move(file_name);
	auto _file_name = m_file_name.string();
#ifdef __APPLE__
	std::vector candidates
	{
		_file_name + ".bundle",
		_file_name + ".dylib",
		_file_name + "." + m_version + ".bundle",
		_file_name + "." + m_version + ".dylib"
	};
#else
	std::vector candidates
	{
		_file_name,
		_file_name + ".so",
		_file_name + ".so." + m_version
	};
	if( _file_name.find('/') == std::string_view::npos )
	{
		candidates.emplace_back("lib" + _file_name);
		candidates.emplace_back("lib" + _file_name + ".so");
		candidates.emplace_back("lib" + _file_name + ".so." + m_version);
	}
#endif
	for(auto &name : candidates)
	{
		auto abs_name = *app::absolute_path(name).or_else("");
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

	auto _file_name = m_file_name.string();
	m_handle = dlopen(_file_name.c_str(), RTLD_LAZY | RTLD_LOCAL);

	if( not m_handle )
		return {errno, g_library_category};
	return {};
}

error_code library::impl::unload_native()
{
	if( dlclose(m_handle) )
		return {errno, g_library_category};
	return {};
}

} //namespace riwo

#endif //__unix__
