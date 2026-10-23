// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "file_handle.h"

#include <utility>

namespace riwo::http_exp
{

owned_c_file::owned_c_file(std::FILE *file) noexcept : m_file(file) {}

owned_c_file::~owned_c_file()
{
	if( m_file )
		(void)std::fclose(m_file);
}

owned_c_file::owned_c_file(owned_c_file &&other) noexcept :
	m_file(std::exchange(other.m_file, nullptr))
{
}

owned_c_file &owned_c_file::operator=(owned_c_file &&other) noexcept
{
	if( this != &other )
	{
		if( m_file )
			(void)std::fclose(m_file);
		m_file = std::exchange(other.m_file, nullptr);
	}
	return *this;
}

std::FILE *owned_c_file::get() const noexcept
{
	return m_file;
}

std::FILE *owned_c_file::release() noexcept
{
	return std::exchange(m_file, nullptr);
}

std::FILE *owned_c_file::operator*() const noexcept
{
	return m_file;
}

borrowed_c_file::borrowed_c_file(std::FILE *file) noexcept : m_file(file) {}

borrowed_c_file::borrowed_c_file(borrowed_c_file &&other) noexcept :
	m_file(std::exchange(other.m_file, nullptr))
{
}

borrowed_c_file &borrowed_c_file::operator=(borrowed_c_file &&other) noexcept
{
	if( this != &other )
		m_file = std::exchange(other.m_file, nullptr);
	return *this;
}

std::FILE *borrowed_c_file::get() const noexcept
{
	return m_file;
}

std::FILE *borrowed_c_file::operator*() const noexcept
{
	return m_file;
}

owned_c_file owned_file(std::FILE *file) noexcept
{
	return owned_c_file(file);
}

borrowed_c_file borrowed_file(std::FILE *file) noexcept
{
	return borrowed_c_file(file);
}

} //namespace riwo::http_exp
