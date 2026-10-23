// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "source.h"
#include <riwo/http_exp/error.h>
#include <fstream>
#include <cerrno>
#ifndef _WIN32
# include <sys/types.h>
#endif

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

namespace
{

[[noreturn]] void throw_file_error(std::errc error = std::errc::io_error) {
	throw std::system_error(std::make_error_code(error));
}

[[nodiscard]] int c_seek(std::FILE *file, int64_t offset, int origin) noexcept
{
#ifdef _WIN32
	return ::_fseeki64(file, offset, origin);
#else
	if constexpr( sizeof(off_t) < sizeof(int64_t) )
	{
		if( offset < static_cast<int64_t>(std::numeric_limits<off_t>::min()) or
			offset > static_cast<int64_t>(std::numeric_limits<off_t>::max()) )
		{
			errno = EOVERFLOW;
			return -1;
		}
	}
	return ::fseeko(file, static_cast<off_t>(offset), origin);
#endif
}

[[nodiscard]] int64_t c_tell(std::FILE *file) noexcept
{
#ifdef _WIN32
	return ::_ftelli64(file);
#else
	const auto value = ::ftello(file);
	if( value < 0 ) return -1;
	if constexpr( sizeof(off_t) > sizeof(int64_t) )
	{
		if( value > static_cast<off_t>(std::numeric_limits<int64_t>::max()) )
		{
			errno = EOVERFLOW;
			return -1;
		}
	}
	return static_cast<int64_t>(value);
#endif
}

class input_file_backend
{
public:
	virtual ~input_file_backend() = default;
	[[nodiscard]] virtual optional<uint64_t> size() const noexcept = 0;
	[[nodiscard]] virtual bool replayable() const noexcept = 0;
	[[nodiscard]] virtual size_t read(mutable_buffer, error_code&) = 0;
	virtual void rewind(error_code&) = 0;
	virtual void finish(error_code&) = 0;
};

template <concepts::input_file_stream Stream>
class stream_input_backend final : public input_file_backend
{
public:
	explicit stream_input_backend(Stream &&stream, optional<file_span> span) :
		m_owned(std::in_place, std::move(stream)), m_stream(&*m_owned)
	{
		initialize(span);
	}

	explicit stream_input_backend(Stream *stream, optional<file_span> span) :
		m_stream(stream), m_borrowed(true)
	{
		try { initialize(span); }
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

	[[nodiscard]] bool replayable() const noexcept override { return true; }

	[[nodiscard]] size_t read(mutable_buffer buffer, error_code &error) override
	{
		error.clear();
		if( buffer.size() == 0 ) return 0;
		if( m_offset == m_length )
		{
			error = eof_error();
			return 0;
		}
		if( not *m_stream )
		{
			error = http_exp::make_error_code(errc::body_length_mismatch);
			return 0;
		}
		const auto remaining = m_length - m_offset;
		const auto requested = static_cast<std::streamsize>(std::min<uint64_t>(
			remaining, std::min<uint64_t>(buffer.size(),
				static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))));
		m_stream->read(static_cast<char*>(buffer.data()), requested);
		const auto count = m_stream->gcount();
		if( count <= 0 )
		{
			error = http_exp::make_error_code(errc::body_length_mismatch);
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
			return;
		}
		m_offset = 0;
		m_finished = false;
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( not m_borrowed or m_finished ) return;
		restore_borrowed(error);
		m_finished = true;
	}

private:
	void restore_borrowed(error_code &error) noexcept
	{
		error.clear();
		if( not m_borrowed or not m_captured ) return;
		try {
			m_stream->clear();
			m_stream->seekg(m_base);
			if( not *m_stream )
				error = make_system_error_code(std::errc::io_error);
			m_stream->clear(m_saved_state);
		}
		catch(...) { error = exception_error(std::current_exception()); }
	}

	void initialize(optional<file_span> span)
	{
		if( not m_stream or not *m_stream ) throw_file_error();
		m_saved_state = m_stream->rdstate();
		m_base = m_stream->tellg();
		if( m_base < 0 ) throw_file_error();
		m_captured = true;
		m_stream->seekg(0, std::ios::end);
		const auto end = m_stream->tellg();
		if( end < m_base ) throw_file_error();
		const auto distance = end - m_base;
		if( distance < 0 ) throw_file_error();
		const auto available = static_cast<uint64_t>(distance);
		const auto offset = span ? span->offset : 0;
		const auto length = span ? span->length : available;
		if( offset > available or length > available - offset or
			offset > static_cast<uint64_t>(
				std::numeric_limits<std::streamoff>::max()) )
			throw_file_error(std::errc::invalid_argument);
		m_window_start = m_base + static_cast<std::streamoff>(offset);
		m_length = length;
		m_stream->clear();
		m_stream->seekg(m_window_start);
		if( not *m_stream ) throw_file_error();
	}

	optional<Stream> m_owned;
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

class c_input_backend final : public input_file_backend
{
public:
	explicit c_input_backend(owned_c_file file, optional<file_span> span) :
		m_owned(std::move(file)), m_file(m_owned.get())
	{
		initialize(span);
	}

	explicit c_input_backend(borrowed_c_file file, optional<file_span> span) :
		m_file(file.get()), m_borrowed(true)
	{
		try { initialize(span); }
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
		if( buffer.size() == 0 ) return 0;
		if( m_length and m_offset == *m_length )
		{
			error = eof_error();
			return 0;
		}
		auto requested = buffer.size();
		if( m_length )
			requested = static_cast<size_t>(std::min<uint64_t>(
				requested, *m_length - m_offset));
		const auto count = std::fread(buffer.data(), 1, requested, m_file);
		if( count != 0 )
		{
			m_offset += count;
			return count;
		}
		if( std::ferror(m_file) )
			error = make_system_error_code(std::errc::io_error);
		else if( m_length and m_offset != *m_length )
			error = http_exp::make_error_code(errc::body_length_mismatch);
		else
			error = eof_error();
		return 0;
	}

	void rewind(error_code &error) override
	{
		error.clear();
		if( not m_replayable )
		{
			error = http_exp::make_error_code(errc::operation_not_supported);
			return;
		}
		std::clearerr(m_file);
		if( c_seek(m_file, m_window_start, SEEK_SET) != 0 )
		{
			error = make_system_error_code(std::errc::io_error);
			return;
		}
		m_offset = 0;
		m_finished = false;
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( not m_borrowed or m_finished ) return;
		restore_borrowed(error);
		m_finished = true;
	}

private:
	void restore_borrowed(error_code &error) noexcept
	{
		error.clear();
		if( not m_borrowed or not m_file ) return;
		std::clearerr(m_file);
		if( m_base >= 0 and c_seek(m_file, m_base, SEEK_SET) != 0 )
			error = make_system_error_code(std::errc::io_error);
		std::clearerr(m_file);
	}

	void initialize(optional<file_span> span)
	{
		if( not m_file ) throw_file_error(std::errc::invalid_argument);
		if( std::feof(m_file) or std::ferror(m_file) ) throw_file_error();
		m_base = c_tell(m_file);
		if( m_base >= 0 and c_seek(m_file, 0, SEEK_END) == 0 )
		{
			const auto end = c_tell(m_file);
			if( end >= m_base and c_seek(m_file, m_base, SEEK_SET) == 0 )
			{
				const auto available = static_cast<uint64_t>(end - m_base);
				const auto offset = span ? span->offset : 0;
				const auto length = span ? span->length : available;
				if( offset > available or length > available - offset or
					offset > static_cast<uint64_t>(
						std::numeric_limits<int64_t>::max() - m_base) )
					throw_file_error(std::errc::invalid_argument);
				m_window_start = m_base + static_cast<int64_t>(offset);
				if( c_seek(m_file, m_window_start, SEEK_SET) != 0 )
					throw_file_error();
				m_length = length;
				m_replayable = true;
				return;
			}
		}
		std::clearerr(m_file);
		if( m_base >= 0 ) (void)c_seek(m_file, m_base, SEEK_SET);
		if( span ) throw_file_error(std::errc::invalid_argument);
		m_base = -1;
		m_window_start = -1;
	}

	owned_c_file m_owned;
	std::FILE *m_file = nullptr;
	bool m_borrowed = false;
	bool m_replayable = false;
	bool m_finished = false;
	int64_t m_base = -1;
	int64_t m_window_start = -1;
	optional<uint64_t> m_length;
	uint64_t m_offset = 0;
};

class output_file_backend
{
public:
	virtual ~output_file_backend() = default;
	virtual void prepare(error_code&) const noexcept = 0;
	[[nodiscard]] virtual bool random_access() const noexcept = 0;
	[[nodiscard]] virtual size_t write(const_buffer, error_code&) = 0;
	[[nodiscard]] virtual size_t write_at(uint64_t, const_buffer, error_code&) = 0;
	virtual void finish(error_code&) = 0;
};

template <concepts::output_file_stream Stream>
class stream_output_backend final : public output_file_backend
{
public:
	explicit stream_output_backend(const std::filesystem::path &path)
		requires std::same_as<Stream,std::ofstream> :
		m_owned(std::in_place, path, std::ios::binary | std::ios::trunc),
		m_stream(&*m_owned)
	{
		if( not *m_stream )
			m_open_error = make_system_error_code(std::errc::io_error);
		else initialize();
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
			error = http_exp::make_error_code(errc::operation_not_permitted);
			return 0;
		}
		size_t total = 0;
		while( total != buffer.size() )
		{
			const auto size = static_cast<std::streamsize>(std::min<size_t>(
				buffer.size() - total,
				static_cast<size_t>(std::numeric_limits<std::streamsize>::max())));
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

	[[nodiscard]] size_t write_at(uint64_t offset, const_buffer buffer,
		error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		const auto maximum = static_cast<uint64_t>(
			std::numeric_limits<std::streamoff>::max());
		if( not m_random_access or m_base_offset < 0 or
			static_cast<uint64_t>(m_base_offset) > maximum or
			offset > maximum - static_cast<uint64_t>(m_base_offset) )
		{
			error = http_exp::make_error_code(errc::operation_not_supported);
			return 0;
		}
		m_stream->seekp(static_cast<std::streamoff>(
			static_cast<uint64_t>(m_base_offset) + offset), std::ios::beg);
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
		if( m_finished ) return;
		if( m_open_error )
		{
			error = m_open_error;
			m_finished = true;
			return;
		}
		try {
			m_stream->flush();
			if( not *m_stream )
				error = make_system_error_code(std::errc::io_error);
		}
		catch(...) { error = exception_error(std::current_exception()); }
		if( m_borrowed and m_random_access )
		{
			try {
				m_stream->clear();
				m_stream->seekp(m_base);
				if( not *m_stream and not error )
					error = make_system_error_code(std::errc::io_error);
				m_stream->clear(m_saved_state);
			}
			catch(...) {
				if( not error ) error = exception_error(std::current_exception());
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
			return;
		}
		try {
			m_saved_state = m_stream->rdstate();
			m_base = m_stream->tellp();
			m_random_access = m_base >= 0;
			if( m_random_access )
				m_base_offset = static_cast<std::streamoff>(m_base);
			if( not m_random_access ) m_stream->clear(m_saved_state);
		}
		catch(...) { m_open_error = exception_error(std::current_exception()); }
	}

	optional<Stream> m_owned;
	Stream *m_stream = nullptr;
	bool m_borrowed = false;
	bool m_random_access = false;
	bool m_finished = false;
	std::ios::iostate m_saved_state = std::ios::goodbit;
	std::streampos m_base {};
	std::streamoff m_base_offset = -1;
	error_code m_open_error;
};

class c_output_backend final : public output_file_backend
{
public:
	explicit c_output_backend(owned_c_file file) :
		m_owned(std::move(file)), m_file(m_owned.get())
	{
		initialize();
	}

	explicit c_output_backend(borrowed_c_file file) :
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
			error = http_exp::make_error_code(errc::operation_not_permitted);
			return 0;
		}
		size_t total = 0;
		while( total != buffer.size() )
		{
			const auto count = std::fwrite(
				static_cast<const std::byte*>(buffer.data()) + total,
				1, buffer.size() - total, m_file);
			if( count == 0 )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
			total += count;
		}
		return total;
	}

	[[nodiscard]] size_t write_at(uint64_t offset, const_buffer buffer,
		error_code &error) override
	{
		error.clear();
		if( m_open_error )
		{
			error = m_open_error;
			return 0;
		}
		if( m_base < 0 or offset > static_cast<uint64_t>(
			std::numeric_limits<int64_t>::max() - m_base) )
		{
			error = http_exp::make_error_code(errc::operation_not_supported);
			return 0;
		}
		if( c_seek(m_file, m_base + static_cast<int64_t>(offset), SEEK_SET) != 0 )
		{
			error = make_system_error_code(std::errc::io_error);
			return 0;
		}
		return write(buffer, error);
	}

	void finish(error_code &error) override
	{
		error.clear();
		if( m_finished ) return;
		if( m_open_error )
		{
			error = m_open_error;
			m_finished = true;
			return;
		}
		if( std::fflush(m_file) != 0 )
			error = make_system_error_code(std::errc::io_error);
		if( m_borrowed )
		{
			std::clearerr(m_file);
			if( m_base >= 0 and c_seek(m_file, m_base, SEEK_SET) != 0 and not error )
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
			return;
		}
		if( std::feof(m_file) or std::ferror(m_file) )
		{
			m_open_error = make_system_error_code(std::errc::io_error);
			return;
		}
		m_base = c_tell(m_file);
		if( m_base < 0 ) std::clearerr(m_file);
	}

	owned_c_file m_owned;
	std::FILE *m_file = nullptr;
	bool m_borrowed = false;
	bool m_finished = false;
	int64_t m_base = -1;
	error_code m_open_error;
};

[[nodiscard]] std::ifstream open_input_file(
	const std::filesystem::path &path)
{
	std::ifstream stream(path, std::ios::binary);
	if( not stream )
		throw_file_error(std::errc::no_such_file_or_directory);
	return stream;
}

} //namespace

owned_c_file::owned_c_file(std::FILE *file) noexcept : m_file(file) {}

owned_c_file::~owned_c_file() {
	if( m_file ) (void)std::fclose(m_file);
}

owned_c_file::owned_c_file(owned_c_file &&other) noexcept :
	m_file(std::exchange(other.m_file, nullptr))
{
}

owned_c_file &owned_c_file::operator=(owned_c_file &&other) noexcept
{
	if( this != &other )
	{
		if( m_file ) (void)std::fclose(m_file);
		m_file = std::exchange(other.m_file, nullptr);
	}
	return *this;
}

std::FILE *owned_c_file::get() const noexcept { return m_file; }
std::FILE *owned_c_file::release() noexcept {
	return std::exchange(m_file, nullptr);
}

borrowed_c_file::borrowed_c_file(std::FILE *file) noexcept : m_file(file) {}

borrowed_c_file::borrowed_c_file(borrowed_c_file &&other) noexcept :
	m_file(std::exchange(other.m_file, nullptr))
{
}

borrowed_c_file &borrowed_c_file::operator=(borrowed_c_file &&other) noexcept
{
	if( this != &other ) m_file = std::exchange(other.m_file, nullptr);
	return *this;
}

std::FILE *borrowed_c_file::get() const noexcept { return m_file; }

owned_c_file owned_file(std::FILE *file) noexcept { return owned_c_file(file); }
borrowed_c_file borrowed_file(std::FILE *file) noexcept {
	return borrowed_c_file(file);
}

class file_body_source::impl
{
public:
	explicit impl(std::unique_ptr<input_file_backend> backend) :
		m_backend(std::move(backend))
	{
	}

	std::unique_ptr<input_file_backend> m_backend;
	bool m_cancelled = false;
};

file_body_source::file_body_source(std::filesystem::path path,
	optional<file_span> span) :
	file_body_source(open_input_file(path), span)
{
}

file_body_source::file_body_source(std::ifstream &&stream,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_input_backend<std::ifstream>>(
			std::move(stream), span)))
{
}

file_body_source::file_body_source(std::fstream &&stream,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_input_backend<std::fstream>>(
			std::move(stream), span)))
{
}

file_body_source::file_body_source(borrowed_file_ref<std::ifstream> stream,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_input_backend<std::ifstream>>(
			stream.get(), span)))
{
}

file_body_source::file_body_source(borrowed_file_ref<std::fstream> stream,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_input_backend<std::fstream>>(
			stream.get(), span)))
{
}

file_body_source::file_body_source(owned_c_file file,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<c_input_backend>(std::move(file), span)))
{
}

file_body_source::file_body_source(borrowed_c_file file,
	optional<file_span> span) :
	m_impl(std::make_unique<impl>(
		std::make_unique<c_input_backend>(std::move(file), span)))
{
}

file_body_source::~file_body_source() = default;
file_body_source::file_body_source(file_body_source &&) noexcept = default;
file_body_source &file_body_source::operator=(file_body_source &&) noexcept = default;

optional<uint64_t> file_body_source::size() const noexcept {
	return m_impl->m_backend->size();
}

bool file_body_source::replayable() const noexcept {
	return m_impl->m_backend->replayable();
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
	try {
		return m_impl->m_backend->read(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
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
	error_code error;
	try {
		m_impl->m_backend->rewind(error);
		if( not error ) m_impl->m_cancelled = false;
	}
	catch(...) { error = exception_error(std::current_exception()); }
	std::move(handler)(error);
}

void file_body_source::finish(error_code &error) noexcept
{
	error.clear();
	try { m_impl->m_backend->finish(error); }
	catch(...) { error = exception_error(std::current_exception()); }
}

void file_body_source::finish(
	asio::any_completion_handler<void(error_code)> handler)
{
	error_code error;
	finish(error);
	std::move(handler)(error);
}

void file_body_source::cancel() noexcept {
	error_code ignored;
	try { m_impl->m_backend->finish(ignored); }
	catch(...) {}
	m_impl->m_cancelled = true;
}

class file_sink::impl
{
public:
	explicit impl(std::unique_ptr<output_file_backend> backend) :
		m_backend(std::move(backend))
	{
	}

	std::unique_ptr<output_file_backend> m_backend;
	bool m_cancelled = false;
	bool m_finished = false;
};

file_sink::file_sink(std::filesystem::path path) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_output_backend<std::ofstream>>(path)))
{
}

file_sink::file_sink(std::ofstream &&stream) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_output_backend<std::ofstream>>(
			std::move(stream))))
{
}

file_sink::file_sink(std::fstream &&stream) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_output_backend<std::fstream>>(
			std::move(stream))))
{
}

file_sink::file_sink(borrowed_file_ref<std::ofstream> stream) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_output_backend<std::ofstream>>(stream.get())))
{
}

file_sink::file_sink(borrowed_file_ref<std::fstream> stream) :
	m_impl(std::make_unique<impl>(
		std::make_unique<stream_output_backend<std::fstream>>(stream.get())))
{
}

file_sink::file_sink(owned_c_file file) :
	m_impl(std::make_unique<impl>(
		std::make_unique<c_output_backend>(std::move(file))))
{
}

file_sink::file_sink(borrowed_c_file file) :
	m_impl(std::make_unique<impl>(
		std::make_unique<c_output_backend>(std::move(file))))
{
}

file_sink::~file_sink() = default;
file_sink::file_sink(file_sink &&) noexcept = default;
file_sink &file_sink::operator=(file_sink &&) noexcept = default;

void file_sink::prepare(error_code &error) const noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return;
	}
	if( m_impl->m_finished )
	{
		error = http_exp::make_error_code(errc::operation_not_permitted);
		return;
	}
	m_impl->m_backend->prepare(error);
}

bool file_sink::random_access() const noexcept {
	return m_impl->m_backend->random_access();
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
	try {
		return m_impl->m_backend->write(buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void file_sink::write(const_buffer buffer,
	asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = write(buffer, error);
	std::move(handler)(error, size);
}

size_t file_sink::write_at(uint64_t offset, const_buffer buffer,
	error_code &error) noexcept
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
	try {
		return m_impl->m_backend->write_at(offset, buffer, error);
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void file_sink::write_at(uint64_t offset, const_buffer buffer,
	asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	auto size = write_at(offset, buffer, error);
	std::move(handler)(error, size);
}

void file_sink::finish(error_code &error) noexcept
{
	error.clear();
	if( m_impl->m_cancelled )
	{
		error = aborted_error();
		return;
	}
	if( m_impl->m_finished ) return;
	try { m_impl->m_backend->finish(error); }
	catch(...) { error = exception_error(std::current_exception()); }
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
	if( m_impl->m_finished ) return;
	error_code ignored;
	try { m_impl->m_backend->finish(ignored); }
	catch(...) {}
	m_impl->m_cancelled = true;
	m_impl->m_finished = true;
}

} //namespace riwo::http_exp

namespace riwo::http_exp::detail
{

body_source::~body_source() = default;

} //namespace riwo::http_exp::detail
