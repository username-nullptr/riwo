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

memory_body_source::memory_body_source() = default;

memory_body_source::memory_body_source(std::vector<std::byte> bytes) :
	bytes_(std::move(bytes))
{
}

memory_body_source::memory_body_source(const_buffer bytes) :
	bytes_(bytes.size())
{
	if( !bytes_.empty() )
		std::memcpy(bytes_.data(), bytes.data(), bytes.size());
}

optional<uint64_t> memory_body_source::size() const noexcept {
	return bytes_.size();
}

bool memory_body_source::replayable() const noexcept {
	return true;
}

size_t memory_body_source::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	if( cancelled_ )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 ) return 0;
	if( offset_ == bytes_.size() )
	{
		error = eof_error();
		return 0;
	}
	const auto size = std::min(buffer.size(), bytes_.size() - offset_);
	std::memcpy(buffer.data(), bytes_.data() + offset_, size);
	offset_ += size;
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
	cancelled_ = false;
	offset_ = 0;
	std::move(handler)(error_code{});
}

void memory_body_source::cancel() noexcept {
	cancelled_ = true;
}

class file_body_source::impl
{
public:
	explicit impl(std::filesystem::path source_path) :
		stream(source_path, std::ios::binary)
	{
		if( !stream )
			throw std::system_error(std::make_error_code(
				std::errc::no_such_file_or_directory));
		stream.seekg(0, std::ios::end);
		const auto end = stream.tellg();
		if( end < 0 )
			throw std::system_error(std::make_error_code(std::errc::io_error));
		length = static_cast<uint64_t>(end);
		stream.seekg(0, std::ios::beg);
		if( !stream )
			throw std::system_error(std::make_error_code(std::errc::io_error));
	}

	std::ifstream stream;
	uint64_t length = 0;
	uint64_t offset = 0;
	bool cancelled = false;
};

file_body_source::file_body_source(std::filesystem::path path) :
	impl_(std::make_unique<impl>(std::move(path)))
{
}

file_body_source::~file_body_source() = default;
file_body_source::file_body_source(file_body_source &&) noexcept = default;
file_body_source &file_body_source::operator=(file_body_source &&) noexcept = default;

optional<uint64_t> file_body_source::size() const noexcept {
	return impl_->length;
}

bool file_body_source::replayable() const noexcept {
	return true;
}

size_t file_body_source::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	if( impl_->cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( buffer.size() == 0 ) return 0;
	if( impl_->offset == impl_->length )
	{
		error = eof_error();
		return 0;
	}
	const auto remaining = impl_->length - impl_->offset;
	const auto requested = static_cast<std::streamsize>(std::min<uint64_t>(
		remaining, std::min<uint64_t>(buffer.size(),
			static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))));
	impl_->stream.read(static_cast<char*>(buffer.data()), requested);
	const auto count = impl_->stream.gcount();
	if( count <= 0 )
	{
		error = http_exp::make_error_code(http_exp::errc::body_length_mismatch);
		return 0;
	}
	impl_->offset += static_cast<uint64_t>(count);
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
	impl_->stream.clear();
	impl_->stream.seekg(0, std::ios::beg);
	if( !impl_->stream )
	{
		std::move(handler)(make_system_error_code(std::errc::io_error));
		return;
	}
	impl_->offset = 0;
	impl_->cancelled = false;
	std::move(handler)(error_code{});
}

void file_body_source::cancel() noexcept {
	impl_->cancelled = true;
}

} //namespace riwo::http_exp

namespace riwo::http_exp::detail
{

body_source::~body_source() = default;

} //namespace riwo::http_exp::detail
