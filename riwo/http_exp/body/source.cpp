// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "source.h"
#include <riwo/http_exp/error.h>
#include <fstream>

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] error_code aborted_error() noexcept {
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code eof_error() noexcept {
	return asio::error::make_error_code(asio::error::eof);
}

} //namespace

class memory_body_source::impl
{
public:
	impl() = default;
	explicit impl(std::vector<std::byte> value) : m_bytes(std::move(value)) {}

	std::vector<std::byte> m_bytes;
	size_t m_offset = 0;
	bool m_cancelled = false;
};

memory_body_source::memory_body_source() : m_impl(std::make_unique<impl>()) {}

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

memory_body_source::~memory_body_source() = default;

memory_body_source::memory_body_source(const memory_body_source &other) :
	m_impl(std::make_unique<impl>(*other.m_impl))
{
}

memory_body_source &memory_body_source::operator=(
	const memory_body_source &other)
{
	if( this != &other ) m_impl = std::make_unique<impl>(*other.m_impl);
	return *this;
}

memory_body_source::memory_body_source(memory_body_source &&) noexcept = default;
memory_body_source &memory_body_source::operator=(
	memory_body_source &&) noexcept = default;

optional<uint64_t> memory_body_source::size() const noexcept {
	return m_impl->m_bytes.size();
}

bool memory_body_source::replayable() const noexcept {
	return true;
}

size_t memory_body_source::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 ) return 0;
	if( m_impl->m_offset == m_impl->m_bytes.size() )
	{
		error = eof_error();
		return 0;
	}
	const auto size = std::min(
		buffer.size(), m_impl->m_bytes.size() - m_impl->m_offset);
	std::memcpy(buffer.data(), m_impl->m_bytes.data() + m_impl->m_offset, size);
	m_impl->m_offset += size;
	return size;
}

void memory_body_source::read_some(mutable_buffer buffer,
	asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = read_some(buffer, error);
	std::move(handler)(error, size);
}

void memory_body_source::rewind(
	asio::any_completion_handler<void(error_code)> handler)
{
	m_impl->m_cancelled = false;
	m_impl->m_offset = 0;
	std::move(handler)(error_code{});
}

void memory_body_source::cancel() noexcept {
	m_impl->m_cancelled = true;
}

class file_body_source::impl
{
public:
	explicit impl(std::filesystem::path source_path) :
		m_stream(source_path, std::ios::binary)
	{
		if( not m_stream )
			throw std::system_error(std::make_error_code(
				std::errc::no_such_file_or_directory));
		m_stream.seekg(0, std::ios::end);
		const auto end = m_stream.tellg();
		if( end < 0 )
			throw std::system_error(std::make_error_code(std::errc::io_error));
		m_length = static_cast<uint64_t>(end);
		m_stream.seekg(0, std::ios::beg);
		if( not m_stream )
			throw std::system_error(std::make_error_code(std::errc::io_error));
	}

	std::ifstream m_stream;
	uint64_t m_length = 0;
	uint64_t m_offset = 0;
	bool m_cancelled = false;
};

file_body_source::file_body_source(std::filesystem::path path) :
	m_impl(std::make_unique<impl>(std::move(path)))
{
}

file_body_source::~file_body_source() = default;
file_body_source::file_body_source(file_body_source &&) noexcept = default;
file_body_source &file_body_source::operator=(file_body_source &&) noexcept = default;

optional<uint64_t> file_body_source::size() const noexcept {
	return m_impl->m_length;
}

bool file_body_source::replayable() const noexcept {
	return true;
}

size_t file_body_source::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 ) return 0;
	if( m_impl->m_offset == m_impl->m_length )
	{
		error = eof_error();
		return 0;
	}
	const auto remaining = m_impl->m_length - m_impl->m_offset;
	const auto requested = static_cast<std::streamsize>(std::min<uint64_t>(
		remaining, std::min<uint64_t>(buffer.size(),
			static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))));
	m_impl->m_stream.read(static_cast<char*>(buffer.data()), requested);
	const auto count = m_impl->m_stream.gcount();
	if( count <= 0 )
	{
		error = http_exp::make_error_code(http_exp::errc::body_length_mismatch);
		return 0;
	}
	m_impl->m_offset += static_cast<uint64_t>(count);
	return static_cast<size_t>(count);
}

void file_body_source::read_some(mutable_buffer buffer,
	asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = read_some(buffer, error);
	std::move(handler)(error, size);
}

void file_body_source::rewind(
	asio::any_completion_handler<void(error_code)> handler)
{
	m_impl->m_stream.clear();
	m_impl->m_stream.seekg(0, std::ios::beg);
	if( not m_impl->m_stream )
	{
		std::move(handler)(make_system_error_code(std::errc::io_error));
		return;
	}
	m_impl->m_offset = 0;
	m_impl->m_cancelled = false;
	std::move(handler)(error_code{});
}

void file_body_source::cancel() noexcept {
	m_impl->m_cancelled = true;
}

} //namespace riwo::http_exp

namespace riwo::http_exp::detail
{

body_source::~body_source() = default;

} //namespace riwo::http_exp::detail
