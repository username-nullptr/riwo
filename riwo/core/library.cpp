// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/library_impl.ipp"

namespace fs = std::filesystem;

namespace riwo
{

library::library(fs::path file_name, std::string_view version) :
	m_impl(new impl(std::move(file_name), version))
{

}

library::~library()
{
	delete m_impl;
}

library::library(library &&other) noexcept :
	m_impl(new impl(std::move(*other.m_impl)))
{

}

library &library::operator=(library &&other) noexcept
{
	if( this != &other )
		*m_impl = std::move(*other.m_impl);
	return *this;
}

sys_expected<> library::load() noexcept
{
	auto error = m_impl->load();
	return error ? sys_expected<>(error) : sys_expected<>();
}

sys_expected<> library::unload() noexcept
{
	auto error = m_impl->unload();
	return error ? sys_expected<>(error) : sys_expected<>();
}

optional<void*> library::interface(std::string_view if_name) const
{
	if( not is_loaded() )
		runtime_error::loc_throw("riwo::library::interface: dll not load.");

	auto ptr = m_impl->interface(if_name);
	return ptr ? optional(ptr) : optional<void*>();
}

bool library::exists(std::string_view if_name) const
{
	if( not is_loaded() )
		runtime_error::loc_throw("riwo::library::exists: dll not load.");
	return m_impl->exists(if_name);
}

bool library::is_loaded() const noexcept
{
	return m_impl->m_handle != nullptr;
}

fs::path library::file_name() const noexcept
{
	return m_impl->m_file_name;
}

} //namespace riwo
