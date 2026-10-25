// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "file.h"
#include <riwo/http_exp/protocol/detail/multipart_utils.h>

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] std::chrono::system_clock::time_point system_time(
	std::filesystem::file_time_type value)
{
	return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		value - std::filesystem::file_time_type::clock::now() +
		std::chrono::system_clock::now());
}

[[nodiscard]] bool if_range_matches(const request_head &request,
	std::string_view etag,
	std::chrono::system_clock::time_point last_modified) noexcept
{
	const auto condition = request.headers.find_first("if-range");
	if( not condition )
		return true;
	if( condition->starts_with("W/") )
		return false;
	if( condition->starts_with('"') )
		return strong_entity_tag_equal(*condition, etag);
	const auto date = parse_http_date(*condition);
	return date and std::chrono::floor<std::chrono::seconds>(last_modified) <=
		std::chrono::floor<std::chrono::seconds>(*date);
}

[[nodiscard]] bool path_has_prefix(const std::filesystem::path &path,
	const std::filesystem::path &prefix) noexcept
{
	auto current = path.begin();
	for(auto expected=prefix.begin(); expected!=prefix.end();
		++expected, ++current)
		if( current == path.end() or *current != *expected )
			return false;
	return true;
}

} //namespace

std::string mime_type(std::filesystem::path path)
{
	const auto extension = strtls::to_lower(path.extension().string());
	static const std::unordered_map<std::string,std::string_view> values {
		{".html", "text/html; charset=utf-8"},
		{".htm", "text/html; charset=utf-8"},
		{".css", "text/css; charset=utf-8"},
		{".js", "application/javascript; charset=utf-8"},
		{".json", "application/json; charset=utf-8"},
		{".xml", "application/xml; charset=utf-8"},
		{".txt", "text/plain; charset=utf-8"},
		{".svg", "image/svg+xml"}, {".png", "image/png"},
		{".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
		{".gif", "image/gif"}, {".webp", "image/webp"},
		{".ico", "image/x-icon"}, {".pdf", "application/pdf"},
		{".wasm", "application/wasm"}, {".zip", "application/zip"},
		{".gz", "application/gzip"}, {".mp3", "audio/mpeg"},
		{".mp4", "video/mp4"}, {".woff", "font/woff"},
		{".woff2", "font/woff2"}, {".ttf", "font/ttf"}
	};
	const auto found = values.find(extension);
	return found == values.end() ? "application/octet-stream" :
		std::string(found->second);
}

sys_expected<std::filesystem::path> resolve_resource_path(
	const std::filesystem::path &root,
	const std::filesystem::path &relative) noexcept
{
	try
	{
		if( root.empty() or relative.empty() or relative.is_absolute() )
			return sys_unexpected(make_system_error_code(
				std::errc::invalid_argument));
		const auto canonical_root = std::filesystem::weakly_canonical(root);
		const auto result = std::filesystem::weakly_canonical(
			canonical_root / relative);
		if( not path_has_prefix(result, canonical_root) )
			return sys_unexpected(make_system_error_code(
				std::errc::permission_denied));
		return result;
	}
	catch(const std::filesystem::filesystem_error &error) {
		return sys_unexpected(error.code());
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

class file_response_source::impl
{
public:
	struct segment
	{
		std::string prefix;
		resolved_byte_range range;
		std::string suffix;
	};

	impl() : body(false) {}

	impl(std::filesystem::path source_path, const request_head &request,
		file_response_options options) : path(std::move(source_path))
	{
		std::error_code std_error;
		if( not std::filesystem::is_regular_file(path, std_error) )
		{
			if( not std_error )
				std_error = std::make_error_code(
				std::errc::no_such_file_or_directory);
			filesystem_error::loc_throw("HTTP file response", path, std_error);
		}
		const auto file_size = std::filesystem::file_size(path, std_error);
		if( std_error )
			filesystem_error::loc_throw(
				"HTTP file response size", path, std_error);
		const auto modified_file_time =
			std::filesystem::last_write_time(path, std_error);
		if( std_error )
			filesystem_error::loc_throw(
				"HTTP file response last write", path, std_error);
		const auto modified = system_time(modified_file_time);
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
			modified.time_since_epoch()).count();
		const auto etag = std::format("\"{:x}-{:x}\"", file_size, seconds);
		const auto type = options.content_type.value_or(mime_type(path));
		head.headers.add("Content-Type", type);
		head.headers.add("ETag", etag);
		head.headers.add("Last-Modified", format_http_date(modified));
		head.headers.add("Accept-Ranges", "bytes");
		if( options.cache_control )
			head.headers.add("Cache-Control", *options.cache_control);
		if( options.content_disposition )
			head.headers.add("Content-Disposition", *options.content_disposition);

		const auto precondition = evaluate_preconditions(request.method,
			request.headers, head.headers, true);
		if( precondition == precondition_result::not_modified )
		{
			head.status = status::not_modified;
			body = false;
			total_size = 0;
			return ;
		}
		if( precondition == precondition_result::precondition_failed )
		{
			head.status = status::precondition_failed;
			body = false;
			total_size = 0;
			return ;
		}

		const auto range_header = request.headers.find_first("range");
		if( (request.method.known() != method::get and
			request.method.known() != method::head) or not range_header or
			not if_range_matches(request, etag, modified) )
		{
			select_complete_representation(file_size);
			return ;
		}
		auto parsed = parse_range_header(*range_header);
		if( not parsed or parsed->unit != "bytes" or
			parsed->ranges.size() > options.max_ranges )
		{
			select_complete_representation(file_size);
			return ;
		}
		auto ranges = resolve_byte_ranges(*parsed, file_size);
		if( ranges.empty() )
		{
			head.status = status::range_not_satisfiable;
			head.headers.add("Content-Range",
				format_unsatisfied_content_range(file_size));
			body = false;
			total_size = 0;
			return ;
		}
		uint64_t selected_bytes = 0;
		for(const auto &range : ranges)
		{
			if( range.length > std::numeric_limits<uint64_t>::max() -
				selected_bytes )
			{ ranges.clear(); break; }
			selected_bytes += range.length;
		}
		if( ranges.empty() or (selected_bytes > file_size and
			selected_bytes - file_size > options.max_range_overhead) )
		{
			select_complete_representation(file_size);
			return ;
		}
		head.status = status::partial_content;
		if( ranges.size() == 1 )
		{
			head.headers.add("Content-Range",
				format_content_range(ranges.front(), file_size));
			segments.push_back({{}, ranges.front(), {}});
			total_size = ranges.front().length;
			open();
			return ;
		}

		const auto boundary = detail::make_multipart_boundary();
		head.headers.set("Content-Type", "multipart/byteranges; boundary=" +
			boundary);
		total_size = 0;
		for(const auto &range : ranges)
		{
			auto prefix = std::format("--{}\r\nContent-Type: {}\r\n"
				"Content-Range: {}\r\n\r\n", boundary, type,
				format_content_range(range, file_size));
			constexpr auto maximum = std::numeric_limits<uint64_t>::max();
			if( prefix.size() > maximum - total_size or
				2 > maximum - total_size - prefix.size() or
				range.length > maximum - total_size - prefix.size() - 2 )
				length_error::loc_throw("multipart byte ranges are too large");
			total_size += prefix.size() + range.length + 2;
			segments.push_back({std::move(prefix), range, "\r\n"});
		}
		closing = "--" + boundary + "--\r\n";
		if( closing.size() > std::numeric_limits<uint64_t>::max() - total_size )
			length_error::loc_throw("multipart byte ranges are too large");
		total_size += closing.size();
		open();
	}

	void select_complete_representation(uint64_t file_size)
	{
		segments.push_back({{}, {0, file_size}, {}});
		total_size = file_size;
		open();
	}

	void open()
	{
		file.close();
		file.clear();
		file.open(path, std::ios::binary);
		if( not file )
			filesystem_error::loc_throw("HTTP file response open", path,
				std::make_error_code(std::errc::io_error));
		segment_index = 0;
		prefix_offset = 0;
		file_offset = 0;
		suffix_offset = 0;
		closing_offset = 0;
		cancelled = false;
	}

	response_head head;
	std::filesystem::path path;
	std::vector<segment> segments;
	std::string closing;
	std::ifstream file;
	uint64_t total_size = 0;
	size_t segment_index = 0;
	size_t prefix_offset = 0;
	uint64_t file_offset = 0;
	size_t suffix_offset = 0;
	size_t closing_offset = 0;
	bool body = true;
	bool cancelled = false;
};

file_response_source::file_response_source(std::filesystem::path path,
	const request_head &request, file_response_options options) :
	m_impl(std::make_unique<impl>(std::move(path), request, std::move(options)))
{
}

file_response_source::~file_response_source() = default;
file_response_source::file_response_source(file_response_source &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>();
}
file_response_source &file_response_source::operator=(
	file_response_source &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>();
	return *this;
}

const response_head &file_response_source::response() const noexcept {
	return m_impl->head;
}
bool file_response_source::has_body() const noexcept { return m_impl->body; }
optional<uint64_t> file_response_source::size() const noexcept {
	return m_impl->total_size;
}
bool file_response_source::replayable() const noexcept { return true; }

size_t file_response_source::read_some(mutable_buffer buffer,
	error_code &error) noexcept
{
	error.clear();
	try
	{
		if( m_impl->cancelled )
		{
			error = asio::error::make_error_code(asio::error::operation_aborted);
			return 0;
		}
		if( buffer.size() == 0 or not m_impl->body )
			return 0;
		auto *output = static_cast<char*>(buffer.data());
		while( m_impl->segment_index < m_impl->segments.size() )
		{
			auto &segment = m_impl->segments[m_impl->segment_index];
			if( m_impl->prefix_offset < segment.prefix.size() )
			{
				const auto count = std::min(buffer.size(),
					segment.prefix.size() - m_impl->prefix_offset);
				std::memcpy(output, segment.prefix.data() + m_impl->prefix_offset,
					count);
				m_impl->prefix_offset += count;
				return count;
			}
			if( m_impl->file_offset < segment.range.length )
			{
				if( m_impl->file_offset == 0 )
				{
					if( segment.range.offset > static_cast<uint64_t>(
						std::numeric_limits<std::streamoff>::max()) )
					{
						error = make_system_error_code(
							std::errc::value_too_large);
						return 0;
					}
					m_impl->file.clear();
					m_impl->file.seekg(static_cast<std::streamoff>(
						segment.range.offset));
					if( not m_impl->file )
					{
						error = make_system_error_code(std::errc::io_error);
						return 0;
					}
				}
				const auto count = static_cast<std::streamsize>(
					std::min<uint64_t>(segment.range.length - m_impl->file_offset,
						std::min<uint64_t>(buffer.size(),
							std::numeric_limits<std::streamsize>::max())));
				m_impl->file.read(output, count);
				const auto read = static_cast<size_t>(m_impl->file.gcount());
				if( read == 0 )
				{
					error = make_system_error_code(std::errc::io_error);
					return 0;
				}
				m_impl->file_offset += read;
				return read;
			}
			if( m_impl->suffix_offset < segment.suffix.size() )
			{
				const auto count = std::min(buffer.size(),
					segment.suffix.size() - m_impl->suffix_offset);
				std::memcpy(output, segment.suffix.data() + m_impl->suffix_offset,
					count);
				m_impl->suffix_offset += count;
				return count;
			}
			++m_impl->segment_index;
			m_impl->prefix_offset = 0;
			m_impl->file_offset = 0;
			m_impl->suffix_offset = 0;
		}
		if( m_impl->closing_offset < m_impl->closing.size() )
		{
			const auto count = std::min(buffer.size(),
				m_impl->closing.size() - m_impl->closing_offset);
			std::memcpy(output, m_impl->closing.data() + m_impl->closing_offset,
				count);
			m_impl->closing_offset += count;
			return count;
		}
		error = asio::error::make_error_code(asio::error::eof);
		return 0;
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return 0;
	}
}

void file_response_source::read_some(mutable_buffer buffer,
	asio::any_completion_handler<void(error_code,size_t)> handler)
{
	error_code error;
	const auto size = read_some(buffer, error);
	std::move(handler)(error, size);
}

void file_response_source::rewind(
	asio::any_completion_handler<void(error_code)> handler)
{
	if( not m_impl->body )
	{
		m_impl->cancelled = false;
		return std::move(handler)(error_code{});
	}
	try { m_impl->open(); std::move(handler)(error_code{}); }
	catch(...) {
		std::move(handler)(exception_error(std::current_exception()));
	}
}

void file_response_source::finish(error_code &error) noexcept {
	error.clear();
}

void file_response_source::finish(
	asio::any_completion_handler<void(error_code)> handler) {
	std::move(handler)(error_code{});
}

void file_response_source::cancel() noexcept {
	m_impl->cancelled = true;
	m_impl->file.close();
}

} //namespace riwo::http_exp
