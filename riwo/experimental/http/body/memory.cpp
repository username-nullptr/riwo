// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "memory.h"

namespace riwo::experimental::http { namespace
{

[[nodiscard]] error_code aborted_error() noexcept
{
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code eof_error() noexcept
{
	return asio::error::make_error_code(asio::error::eof);
}

} //namespace

memory_body_source::memory_body_source(std::vector<std::byte> bytes) :
	m_bytes(std::move(bytes))
{
}

memory_body_source::memory_body_source(const const_buffer &bytes) :
	m_bytes(bytes.size())
{
	if( not m_bytes.empty() )
		std::memcpy(m_bytes.data(), bytes.data(), bytes.size());
}

memory_body_source::memory_body_source() = default;

memory_body_source::~memory_body_source() = default;

memory_body_source::memory_body_source(const memory_body_source &other) = default;

memory_body_source &memory_body_source::operator=(
	const memory_body_source &other) = default;

memory_body_source::memory_body_source(memory_body_source &&other) noexcept :
	m_bytes(std::move(other.m_bytes)),
	m_offset(std::exchange(other.m_offset, 0)),
	m_cancelled(std::exchange(other.m_cancelled, false))
{
	other.m_bytes.clear();
}

memory_body_source &memory_body_source::operator=(memory_body_source &&other) noexcept
{
	if( this == &other )
		return *this;
	m_bytes = std::move(other.m_bytes);
	m_offset = std::exchange(other.m_offset, 0);
	m_cancelled = std::exchange(other.m_cancelled, false);
	other.m_bytes.clear();
	return *this;
}

size_t memory_body_source::read_some(const mutable_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	if( m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 )
		return 0;

	if( m_offset == m_bytes.size() )
	{
		error = eof_error();
		return 0;
	}
	const auto size = std::min (
		buffer.size(), m_bytes.size() - m_offset
	);
	std::memcpy(buffer.data(), m_bytes.data() + m_offset, size);
	m_offset += size;
	return size;
}

void memory_body_source::read_some
(const mutable_buffer &buffer, asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = read_some(buffer, error);
	std::move(handler)(error, size);
}

void memory_body_source::rewind(asio::any_completion_handler<void(error_code)> handler)
{
	m_cancelled = false;
	m_offset = 0;
	std::move(handler)(error_code{});
}

void memory_body_source::cancel() noexcept
{
	m_cancelled = true;
}

optional<uint64_t> memory_body_source::size() const noexcept
{
	return m_bytes.size();
}

} //namespace riwo::experimental::http
