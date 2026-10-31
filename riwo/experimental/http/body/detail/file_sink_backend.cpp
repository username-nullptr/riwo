// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "file_sink_backend.h"
#include "c_file_position.h"
#include <riwo/experimental/http/error.h>

namespace riwo::experimental::http::detail { namespace
{

template <concepts::output_file_stream Stream>
class RIWO_DECL_HIDDEN stream_output_backend final : public output_file_backend
{
public:
	explicit stream_output_backend(const std::filesystem::path &path)
		requires std::same_as<Stream,std::ofstream> :
		m_owned(std::in_place, path, std::ios::binary | std::ios::trunc),
		m_stream(&*m_owned)
	{
		if( not *m_stream )
			m_open_error = make_system_error_code(std::errc::io_error);
		else
			initialize();
	}

	explicit stream_output_backend(Stream &&stream) :
		m_owned(std::in_place, std::move(stream)), m_stream(&*m_owned)
	{
		initialize();
	}

	explicit stream_output_backend(Stream *stream) :
		m_stream(stream), m_borrowed(true)
	{
		initialize();
	}

	~stream_output_backend() override
	{
		try {
			error_code ignored;
			finish(ignored);
		}
		catch(...) {}
	}

	void prepare(error_code &error) const noexcept override {
		error = m_open_error;
	}

	[[nodiscard]] bool random_access() const noexcept override {
		return m_random_access;
	}

	[[nodiscard]] size_t write(const_buffer buffer, error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		if( m_finished )
		{
			error = make_error_code(errc::operation_not_permitted);
			return 0;
		}
		size_t total = 0;
		while( total != buffer.size() )
		{
			const auto size = static_cast<std::streamsize>(std::min<size_t>(
				buffer.size() - total, std::numeric_limits<std::streamsize>::max()
			));
			m_stream->write(static_cast<const char*>(buffer.data()) + total, size);
			if( not *m_stream )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
			total += static_cast<size_t>(size);
		}
		return total;
	}

	[[nodiscard]] size_t write_at(uint64_t offset, const_buffer buffer, error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		const auto maximum = static_cast<uint64_t>(
			std::numeric_limits<std::streamoff>::max()
		);
		if( not m_random_access or m_base_offset < 0 or
			static_cast<uint64_t>(m_base_offset) > maximum or
			offset > maximum - static_cast<uint64_t>(m_base_offset) )
		{
			error = make_error_code(errc::operation_not_supported);
			return 0;
		}
		m_stream->seekp(static_cast<std::streamoff>(
			static_cast<uint64_t>(m_base_offset) + offset), std::ios::beg
		);
		if( not *m_stream )
		{
			error = make_system_error_code(std::errc::io_error);
			return 0;
		}
		return write(buffer, error);
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( m_finished )
			return ;

		if( m_open_error )
		{
			error = m_open_error;
			m_finished = true;
			return ;
		}
		try {
			m_stream->flush();
			if( not *m_stream )
				error = make_system_error_code(std::errc::io_error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		if( m_borrowed and m_random_access )
		{
			try {
				m_stream->clear();
				m_stream->seekp(m_base);

				if( not *m_stream and not error )
					error = make_system_error_code(std::errc::io_error);

				m_stream->clear(m_saved_state);
			}
			catch(...)
			{
				if( not error )
					error = exception_error(std::current_exception());
			}
		}
		m_finished = true;
	}

private:
	void initialize()
	{
		if( not m_stream or not *m_stream )
		{
			m_open_error = make_system_error_code(std::errc::io_error);
			return ;
		}
		try {
			m_saved_state = m_stream->rdstate();
			m_base = m_stream->tellp();

			m_random_access = m_base >= 0;
			if( m_random_access )
				m_base_offset = static_cast<std::streamoff>(m_base);

			if( not m_random_access )
				m_stream->clear(m_saved_state);
		}
		catch(...) {
			m_open_error = exception_error(std::current_exception());
		}
	}

	optional<Stream> m_owned {};
	Stream *m_stream = nullptr;

	bool m_borrowed = false;
	bool m_random_access = false;
	bool m_finished = false;

	std::ios::iostate m_saved_state = std::ios::goodbit;
	std::streampos m_base {};
	std::streamoff m_base_offset = -1;

	error_code m_open_error {};
};

class RIWO_DECL_HIDDEN c_output_backend final : public output_file_backend
{
public:
	explicit c_output_backend(owned_c_file file) :
		m_owned(std::move(file)), m_file(m_owned.get())
	{
		initialize();
	}

	explicit c_output_backend(const borrowed_c_file &file) :
		m_file(file.get()), m_borrowed(true)
	{
		initialize();
	}

	~c_output_backend() override
	{
		try {
			error_code ignored;
			finish(ignored);
		}
		catch(...) {}
	}

	void prepare(error_code &error) const noexcept override {
		error = m_open_error;
	}

	[[nodiscard]] bool random_access() const noexcept override {
		return m_base >= 0;
	}

	[[nodiscard]] size_t write(const_buffer buffer, error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		if( m_finished )
		{
			error = make_error_code(errc::operation_not_permitted);
			return 0;
		}
		size_t total = 0;
		while( total != buffer.size() )
		{
			const auto count = std::fwrite (
				static_cast<const std::byte*>(buffer.data()) + total,
				1, buffer.size() - total, m_file
			);
			if( count == 0 )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
			total += count;
		}
		return total;
	}

	[[nodiscard]] size_t write_at
	(uint64_t offset, const_buffer buffer, error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		if( m_base < 0 or offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - m_base) )
		{
			error = make_error_code(errc::operation_not_supported);
			return 0;
		}
		if( c_file_seek(m_file, m_base + static_cast<int64_t>(offset), SEEK_SET) != 0 )
		{
			error = make_system_error_code(std::errc::io_error);
			return 0;
		}
		return write(buffer, error);
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( m_finished )
			return ;

		if( m_open_error )
		{
			error = m_open_error;
			m_finished = true;
			return ;
		}
		if( std::fflush(m_file) != 0 )
			error = make_system_error_code(std::errc::io_error);

		if( m_borrowed )
		{
			std::clearerr(m_file);

			if( m_base >= 0 and c_file_seek(m_file, m_base, SEEK_SET) != 0 and not error )
				error = make_system_error_code(std::errc::io_error);

			std::clearerr(m_file);
		}
		m_finished = true;
	}

private:
	void initialize()
	{
		if( not m_file )
		{
			m_open_error = make_system_error_code(std::errc::invalid_argument);
			return ;
		}
		if( std::feof(m_file) or std::ferror(m_file) )
		{
			m_open_error = make_system_error_code(std::errc::io_error);
			return ;
		}
		m_base = c_file_tell(m_file);
		if( m_base < 0 )
			std::clearerr(m_file);
	}

	owned_c_file m_owned {};
	std::FILE *m_file = nullptr;

	bool m_borrowed = false;
	bool m_finished = false;

	int64_t m_base = -1;
	error_code m_open_error {};
};

} //namespace

output_file_backend_ptr make_output_file_backend(const std::filesystem::path &path)
{
	return std::make_unique<stream_output_backend<std::ofstream>>(path);
}

output_file_backend_ptr make_output_file_backend(std::ofstream &&stream)
{
	return std::make_unique<stream_output_backend<std::ofstream>>(std::move(stream));
}

output_file_backend_ptr make_output_file_backend(std::fstream &&stream)
{
	return std::make_unique<stream_output_backend<std::fstream>>(std::move(stream));
}

output_file_backend_ptr make_output_file_backend(std::ofstream *stream)
{
	return std::make_unique<stream_output_backend<std::ofstream>>(stream);
}

output_file_backend_ptr make_output_file_backend(std::fstream *stream)
{
	return std::make_unique<stream_output_backend<std::fstream>>(stream);
}

output_file_backend_ptr make_output_file_backend(owned_c_file file)
{
	return std::make_unique<c_output_backend>(std::move(file));
}

output_file_backend_ptr make_output_file_backend(borrowed_c_file file)
{
	return std::make_unique<c_output_backend>(std::move(file));
}

} //namespace riwo::experimental::http::detail
