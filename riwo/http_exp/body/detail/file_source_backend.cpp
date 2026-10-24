// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "file_source_backend.h"
#include "c_file_position.h"
#include <riwo/http_exp/error.h>

namespace riwo::http_exp::detail { namespace
{

[[noreturn]] void throw_file_error(std::errc error = std::errc::io_error)
{
	throw std::system_error(std::make_error_code(error));
}

[[nodiscard]] error_code eof_error() noexcept
{
	return asio::error::make_error_code(asio::error::eof);
}

template <concepts::input_file_stream Stream>
class RIWO_DECL_HIDDEN stream_input_backend final : public input_file_backend
{
public:
	explicit stream_input_backend(Stream &&stream, const optional<file_span> &span) :
		m_owned(std::in_place, std::move(stream)), m_stream(&*m_owned)
	{
		initialize(span);
	}

	explicit stream_input_backend(Stream *stream, const optional<file_span> &span) :
		m_stream(stream), m_borrowed(true)
	{
		try {
			initialize(span);
		}
		catch(...)
		{
			error_code ignored;
			restore_borrowed(ignored);
			throw;
		}
	}

	~stream_input_backend() override
	{
		try {
			error_code ignored;
			finish(ignored);
		}
		catch(...) {}
	}

	[[nodiscard]] optional<uint64_t> size() const noexcept override {
		return m_length;
	}

	[[nodiscard]] bool replayable() const noexcept override {
		return true;
	}

	[[nodiscard]] size_t read(mutable_buffer buffer, error_code &error) override
	{
		error.clear();
		if( buffer.size() == 0 )
			return 0;

		if( m_offset == m_length )
		{
			error = eof_error();
			return 0;
		}
		if( not *m_stream )
		{
			error = make_error_code(errc::body_length_mismatch);
			return 0;
		}
		const auto remaining = m_length - m_offset;

		const auto requested = static_cast<std::streamsize>(std::min<uint64_t>(
			remaining, std::min<uint64_t>(buffer.size(), std::numeric_limits<std::streamsize>::max())
		));
		m_stream->read(static_cast<char*>(buffer.data()), requested);
		const auto count = m_stream->gcount();

		if( count <= 0 )
		{
			error = make_error_code(errc::body_length_mismatch);
			return 0;
		}
		m_offset += static_cast<uint64_t>(count);
		return static_cast<size_t>(count);
	}

	void rewind(error_code &error) override
	{
		error.clear();
		m_stream->clear();
		m_stream->seekg(m_window_start);

		if( not *m_stream )
		{
			error = make_system_error_code(std::errc::io_error);
			return ;
		}
		m_offset = 0;
		m_finished = false;
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( not m_borrowed or m_finished )
			return ;
		restore_borrowed(error);
		m_finished = true;
	}

private:
	void restore_borrowed(error_code &error) noexcept
	{
		error.clear();
		if( not m_borrowed or not m_captured )
			return ;
		try {
			m_stream->clear();
			m_stream->seekg(m_base);

			if( not *m_stream )
				error = make_system_error_code(std::errc::io_error);
			m_stream->clear(m_saved_state);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
	}

	void initialize(const optional<file_span> &span)
	{
		if( not m_stream or not *m_stream )
			throw_file_error();

		m_saved_state = m_stream->rdstate();
		m_base = m_stream->tellg();

		if( m_base < 0 )
			throw_file_error();

		m_captured = true;
		m_stream->seekg(0, std::ios::end);

		const auto end = m_stream->tellg();
		if( end < m_base )
			throw_file_error();

		const auto distance = end - m_base;
		if( distance < 0 )
			throw_file_error();

		const auto available = static_cast<uint64_t>(distance);
		const auto offset = span ? span->offset : 0;
		const auto length = span ? span->length : available;

		if( offset > available or length > available - offset or
			offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) )
			throw_file_error(std::errc::invalid_argument);

		m_window_start = m_base + static_cast<std::streamoff>(offset);
		m_length = length;

		m_stream->clear();
		m_stream->seekg(m_window_start);

		if( not *m_stream )
			throw_file_error();
	}

	optional<Stream> m_owned {};
	Stream *m_stream = nullptr;

	bool m_borrowed = false;
	bool m_captured = false;
	bool m_finished = false;

	std::ios::iostate m_saved_state = std::ios::goodbit;
	std::streampos m_base {};
	std::streampos m_window_start {};

	uint64_t m_length = 0;
	uint64_t m_offset = 0;
};

class RIWO_DECL_HIDDEN c_input_backend final : public input_file_backend
{
public:
	explicit c_input_backend(owned_c_file file, const optional<file_span> &span) :
		m_owned(std::move(file)), m_file(m_owned.get())
	{
		initialize(span);
	}

	explicit c_input_backend(const borrowed_c_file &file, const optional<file_span> &span) :
		m_file(file.get()), m_borrowed(true)
	{
		try {
			initialize(span);
		}
		catch(...)
		{
			error_code ignored;
			restore_borrowed(ignored);
			throw;
		}
	}

	~c_input_backend() override
	{
		try {
			error_code ignored;
			finish(ignored);
		}
		catch(...) {}
	}

	[[nodiscard]] optional<uint64_t> size() const noexcept override {
		return m_length;
	}

	[[nodiscard]] bool replayable() const noexcept override {
		return m_replayable;
	}

	[[nodiscard]] size_t read(mutable_buffer buffer, error_code &error) override
	{
		error.clear();
		if( buffer.size() == 0 )
			return 0;

		if( m_length and m_offset == *m_length )
		{
			error = eof_error();
			return 0;
		}
		auto requested = buffer.size();
		if( m_length )
		{
			requested = static_cast<size_t>(std::min<uint64_t>(
				requested, *m_length - m_offset
			));
		}
		if( const auto count = std::fread(buffer.data(), 1, requested, m_file); count != 0 )
		{
			m_offset += count;
			return count;
		}
		if( std::ferror(m_file) )
			error = make_system_error_code(std::errc::io_error);

		else if( m_length and m_offset != *m_length )
			error = make_error_code(errc::body_length_mismatch);
		else
			error = eof_error();
		return 0;
	}

	void rewind(error_code &error) override
	{
		error.clear();
		if( not m_replayable )
		{
			error = make_error_code(errc::operation_not_supported);
			return ;
		}
		std::clearerr(m_file);

		if( c_file_seek(m_file, m_window_start, SEEK_SET) != 0 )
		{
			error = make_system_error_code(std::errc::io_error);
			return ;
		}
		m_offset = 0;
		m_finished = false;
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( not m_borrowed or m_finished )
			return ;
		restore_borrowed(error);
		m_finished = true;
	}

private:
	void restore_borrowed(error_code &error) noexcept
	{
		error.clear();
		if( not m_borrowed or not m_file )
			return ;

		std::clearerr(m_file);

		if( m_base >= 0 and c_file_seek(m_file, m_base, SEEK_SET) != 0 )
			error = make_system_error_code(std::errc::io_error);

		std::clearerr(m_file);
	}

	void initialize(optional<file_span> span)
	{
		if( not m_file )
			throw_file_error(std::errc::invalid_argument);

		if( std::feof(m_file) or std::ferror(m_file) )
			throw_file_error();

		m_base = c_file_tell(m_file);
		if( m_base >= 0 and c_file_seek(m_file, 0, SEEK_END) == 0 )
		{
			const auto end = c_file_tell(m_file);
			if( end >= m_base and c_file_seek(m_file, m_base, SEEK_SET) == 0 )
			{
				const auto available = static_cast<uint64_t>(end - m_base);
				const auto offset = span ? span->offset : 0;
				const auto length = span ? span->length : available;

				if( offset > available or length > available - offset or
					offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - m_base) )
					throw_file_error(std::errc::invalid_argument);

				m_window_start = m_base + static_cast<int64_t>(offset);
				if( c_file_seek(m_file, m_window_start, SEEK_SET) != 0 )
					throw_file_error();

				m_length = length;
				m_replayable = true;
				return ;
			}
		}
		std::clearerr(m_file);
		if( m_base >= 0 )
			(void)c_file_seek(m_file, m_base, SEEK_SET);

		if( span )
			throw_file_error(std::errc::invalid_argument);

		m_base = -1;
		m_window_start = -1;
	}

	owned_c_file m_owned {};
	std::FILE *m_file = nullptr;

	bool m_borrowed = false;
	bool m_replayable = false;
	bool m_finished = false;

	int64_t m_base = -1;
	int64_t m_window_start = -1;

	optional<uint64_t> m_length {};
	uint64_t m_offset = 0;
};

} //namespace

input_file_backend_ptr make_input_file_backend(std::ifstream &&stream, optional<file_span> span)
{
	return std::make_unique<stream_input_backend<std::ifstream>>(std::move(stream), span);
}

input_file_backend_ptr make_input_file_backend(std::fstream &&stream, optional<file_span> span)
{
	return std::make_unique<stream_input_backend<std::fstream>>(std::move(stream), span);
}

input_file_backend_ptr make_input_file_backend(std::ifstream *stream, optional<file_span> span)
{
	return std::make_unique<stream_input_backend<std::ifstream>>(stream, span);
}

input_file_backend_ptr make_input_file_backend(std::fstream *stream, optional<file_span> span)
{
	return std::make_unique<stream_input_backend<std::fstream>>(stream, span);
}

input_file_backend_ptr make_input_file_backend(owned_c_file file, optional<file_span> span)
{
	return std::make_unique<c_input_backend>(std::move(file), span);
}

input_file_backend_ptr make_input_file_backend(borrowed_c_file file, optional<file_span> span)
{
	return std::make_unique<c_input_backend>(std::move(file), span);
}

input_file_backend_ptr make_input_file_backend
(const std::filesystem::path &path, const optional<file_span> &span)
{
	std::ifstream stream(path, std::ios::binary);
	if( not stream )
		throw_file_error(std::errc::no_such_file_or_directory);
	return make_input_file_backend(std::move(stream), span);
}

} //namespace riwo::http_exp::detail
