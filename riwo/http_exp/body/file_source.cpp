// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/file_source_backend.h"
#include <riwo/http_exp/error.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] error_code aborted_error() noexcept
{
	return asio::error::make_error_code(asio::error::operation_aborted);
}

} //namespace

class RIWO_DECL_HIDDEN file_body_source::impl
{
public:
	explicit impl(detail::input_file_backend_ptr backend) :
		m_backend(std::move(backend)) {}

	detail::input_file_backend_ptr m_backend;
	bool m_cancelled = false;
};

file_body_source::file_body_source(std::ifstream &&stream, const optional<file_span> &span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(std::move(stream), span)))
{

}

file_body_source::file_body_source(std::fstream &&stream, const optional<file_span> &span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(std::move(stream), span)))
{

}

file_body_source::file_body_source(const borrowed_file_ref<std::ifstream> &stream, optional<file_span> span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(stream.get(), span)))
{

}

file_body_source::file_body_source(const borrowed_file_ref<std::fstream> &stream, optional<file_span> span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(stream.get(), span)))
{

}

file_body_source::file_body_source(owned_c_file file, const optional<file_span> &span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(std::move(file), span)))
{
}

file_body_source::file_body_source(borrowed_c_file file, const optional<file_span> &span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(std::move(file), span)))
{

}

file_body_source::file_body_source(const std::filesystem::path &path, const optional<file_span> &span) :
	m_impl(std::make_unique<impl>(detail::make_input_file_backend(path, span)))
{

}

file_body_source::~file_body_source() = default;

file_body_source::file_body_source(file_body_source &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(nullptr);
}

file_body_source &file_body_source::operator=(file_body_source &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(nullptr);
	return *this;
}

void file_body_source::read_some
(const mutable_buffer &buffer, asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = read_some(buffer, error);
	std::move(handler)(error, size);
}

size_t file_body_source::read_some(const mutable_buffer &buffer, error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( not m_impl->m_backend )
	{
		error = make_error_code(errc::operation_not_permitted);
		return 0;
	}
	try {
		return m_impl->m_backend->read(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	return 0;
}

void file_body_source::rewind(asio::any_completion_handler<void(error_code)> handler)
{
	error_code error;
	if( not m_impl->m_backend )
	{
		error = make_error_code(errc::operation_not_permitted);
		return std::move(handler)(error);
	}
	try {
		m_impl->m_backend->rewind(error);
		if( not error )
			m_impl->m_cancelled = false;
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	std::move(handler)(error);
}

void file_body_source::finish(error_code &error) noexcept
{
	error.clear();
	if( not m_impl->m_backend )
	{
		error = make_error_code(errc::operation_not_permitted);
		return ;
	}
	try {
		m_impl->m_backend->finish(error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
}

void file_body_source::finish(asio::any_completion_handler<void(error_code)> handler)
{
	error_code error;
	finish(error);
	std::move(handler)(error);
}

void file_body_source::cancel() noexcept
{
	if( not m_impl->m_backend )
	{
		m_impl->m_cancelled = true;
		return ;
	}
	try {
		error_code ignored;
		m_impl->m_backend->finish(ignored);
		RIWO_UNUSED(ignored);
	}
	catch(...) {}
	m_impl->m_cancelled = true;
}

optional<uint64_t> file_body_source::size() const noexcept
{
	return m_impl->m_backend ? m_impl->m_backend->size() : optional<uint64_t>{};
}

bool file_body_source::replayable() const noexcept
{
	return m_impl->m_backend and m_impl->m_backend->replayable();
}

} //namespace riwo::http_exp
