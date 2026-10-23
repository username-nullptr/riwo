// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/file_sink_backend.h"
#include <riwo/http_exp/error.h>

#include <exception>
#include <utility>

namespace riwo::http_exp { namespace
{

[[nodiscard]] error_code aborted_error() noexcept
{
	return asio::error::make_error_code(asio::error::operation_aborted);
}

} //namespace

class RIWO_DECL_HIDDEN file_sink::impl
{
public:
	explicit impl(detail::output_file_backend_ptr backend) :
		m_backend(std::move(backend))
	{
	}

	detail::output_file_backend_ptr m_backend;
	bool m_cancelled = false;
	bool m_finished = false;
};

file_sink::file_sink(std::filesystem::path path) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(path)))
{
}

file_sink::file_sink(std::ofstream &&stream) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(std::move(stream))))
{
}

file_sink::file_sink(std::fstream &&stream) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(std::move(stream))))
{
}

file_sink::file_sink(borrowed_file_ref<std::ofstream> stream) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(stream.get())))
{
}

file_sink::file_sink(borrowed_file_ref<std::fstream> stream) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(stream.get())))
{
}

file_sink::file_sink(owned_c_file file) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(std::move(file))))
{
}

file_sink::file_sink(borrowed_c_file file) :
	m_impl(std::make_unique<impl>(
		detail::make_output_file_backend(std::move(file))))
{
}

file_sink::~file_sink() = default;
file_sink::file_sink(file_sink &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(nullptr);
}

file_sink &file_sink::operator=(file_sink &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(nullptr);
	return *this;
}

void file_sink::prepare(error_code &error) const noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return ;
	}
	if( m_impl->m_finished )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return ;
	}
	if( not m_impl->m_backend )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return ;
	}
	m_impl->m_backend->prepare(error);
}

bool file_sink::random_access() const noexcept
{
	return m_impl->m_backend and m_impl->m_backend->random_access();
}

void file_sink::write
(const_buffer buffer, asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = write(buffer, error);
	std::move(handler)(error, size);
}

size_t file_sink::write(const_buffer buffer, error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( m_impl->m_finished )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return 0;
	}
	if( not m_impl->m_backend )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return 0;
	}
	try {
		return m_impl->m_backend->write(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void file_sink::write_at
(uint64_t offset, const_buffer buffer, asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = write_at(offset, buffer, error);
	std::move(handler)(error, size);
}

size_t file_sink::write_at
(uint64_t offset, const_buffer buffer, error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return 0;
	}
	if( m_impl->m_finished )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return 0;
	}
	if( not m_impl->m_backend )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return 0;
	}
	try {
		return m_impl->m_backend->write_at(offset, buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void file_sink::finish(error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return ;
	}
	if( m_impl->m_finished )
		return ;
	if( not m_impl->m_backend )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return ;
	}
	try { m_impl->m_backend->finish(error); }
	catch(...) {
		error = exception_error(std::current_exception());
	}
	m_impl->m_finished = true;
}

void file_sink::finish(
	asio::any_completion_handler<void(error_code)> handler)
{
	error_code error;
	finish(error);
	std::move(handler)(error);
}

void file_sink::cancel() noexcept
{
	if( m_impl->m_finished )
		return ;
	if( not m_impl->m_backend )
	{
		m_impl->m_cancelled = true;
		m_impl->m_finished = true;
		return ;
	}
	error_code ignored;
	try { m_impl->m_backend->finish(ignored); }
	catch(...) {
	}
	m_impl->m_cancelled = true;
	m_impl->m_finished = true;
}

} //namespace riwo::http_exp
