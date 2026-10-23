// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "memory.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace riwo::http_exp { namespace
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

class RIWO_DECL_HIDDEN memory_body_source::impl
{
public:
	impl() = default;
	explicit impl(std::vector<std::byte> value) :
		m_bytes(std::move(value)) {}

	std::vector<std::byte> m_bytes {};
	size_t m_offset = 0;
	bool m_cancelled = false;
};

memory_body_source::memory_body_source(std::vector<std::byte> bytes) :
	m_impl(std::make_unique<impl>(std::move(bytes)))
{
}

memory_body_source::memory_body_source(const_buffer bytes) :
	m_impl(std::make_unique<impl>(std::vector<std::byte>(bytes.size())))
{
	if( not m_impl->m_bytes.empty() )
		std::memcpy(m_impl->m_bytes.data(), bytes.data(), bytes.size());
}

memory_body_source::memory_body_source() :
	m_impl(std::make_unique<impl>())
{
}

memory_body_source::~memory_body_source() = default;

memory_body_source::memory_body_source(const memory_body_source &other) :
	m_impl(std::make_unique<impl>(*other.m_impl))
{
}

memory_body_source &memory_body_source::operator=(const memory_body_source &other)
{
	if( this != &other )
		m_impl = std::make_unique<impl>(*other.m_impl);
	return *this;
}

memory_body_source::memory_body_source(memory_body_source &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>();
}

memory_body_source &memory_body_source::operator=(
	memory_body_source &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>();
	return *this;
}

size_t memory_body_source::read_some(mutable_buffer buffer, error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 )
		return 0;

	if( m_impl->m_offset == m_impl->m_bytes.size() )
	{
		error = eof_error();
		return 0;
	}
	const auto size = std::min(
		buffer.size(), m_impl->m_bytes.size() - m_impl->m_offset
	);
	std::memcpy(buffer.data(), m_impl->m_bytes.data() + m_impl->m_offset, size);
	m_impl->m_offset += size;
	return size;
}

void memory_body_source::read_some
(mutable_buffer buffer, asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = read_some(std::move(buffer), error);
	std::move(handler)(error, size);
}

void memory_body_source::rewind(asio::any_completion_handler<void(error_code)> handler)
{
	m_impl->m_cancelled = false;
	m_impl->m_offset = 0;
	std::move(handler)(error_code{});
}

void memory_body_source::cancel() noexcept
{
	m_impl->m_cancelled = true;
}

optional<uint64_t> memory_body_source::size() const noexcept
{
	return m_impl->m_bytes.size();
}

bool memory_body_source::replayable() const noexcept
{
	return true;
}

} //namespace riwo::http_exp
