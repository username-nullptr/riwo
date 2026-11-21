// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_DETAIL_TEMPORARY_H
#define RIWO_SYSTEM_DETAIL_TEMPORARY_H

namespace riwo::system { namespace detail
{

[[nodiscard]] RIWO_SYSTEM_API sys_expected<std::filesystem::path>
create_temporary_path(const std::filesystem::path &parent, bool directory);

} //namespace detail

template <bool Directory>
basic_temp_path<Directory>::basic_temp_path(std::filesystem::path path) noexcept :
	m_path(std::move(path))
{

}

template <bool Directory>
basic_temp_path<Directory>::basic_temp_path(basic_temp_path &&other) noexcept :
m_path(other.release())
{

}

template <bool Directory>
basic_temp_path<Directory> &basic_temp_path<Directory>::operator=(basic_temp_path &&other) noexcept
{
	if( this != &other )
	{
		basic_temp_path previous(std::move(*this));
		m_path = other.release();
	}
	return *this;
}

template <bool Directory>
basic_temp_path<Directory>::~basic_temp_path()
{
	try {
		RIWO_UNUSED(remove());
	}
	catch(...) {}
}

template <bool Directory>
auto basic_temp_path<Directory>::create(const std::filesystem::path &parent)
	-> expected<basic_temp_path,error_code>
{
	auto path = detail::create_temporary_path(parent, Directory);
	if( not path )
		return sys_unexpected(path.error());
	return basic_temp_path(std::move(*path));
}

	// Keep the resource and transfer its path to the caller.
template <bool Directory>
std::filesystem::path basic_temp_path<Directory>::release() noexcept
{
	return std::exchange(m_path, {});
}

template <bool Directory>
sys_expected<> basic_temp_path<Directory>::remove()
{
	if( m_path.empty() )
		return {};

	std::error_code error {};
	if constexpr( Directory )
		std::filesystem::remove_all(m_path, error);
	else
		std::filesystem::remove(m_path, error);

	if( error )
		return sys_unexpected(error);

	m_path.clear();
	return {};
}

template <bool Directory>
const std::filesystem::path &basic_temp_path<Directory>::path() const noexcept
{
	return m_path;
}

} //namespace riwo::system


#endif //RIWO_SYSTEM_DETAIL_TEMPORARY_H
