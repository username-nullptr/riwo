// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "compression.h"
#include <riwo/core/string_vector.h>

#if RIWO_HTTP_ZLIB_SUPPORT
# include <zlib.h>
#endif //RIWO_HTTP_ZLIB_SUPPORT

namespace riwo::http { namespace
{

[[nodiscard]] std::string_view trim_view(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] std::string_view media_type(std::string_view value) noexcept {
	return trim_view(value.substr(0, value.find(';')));
}

[[nodiscard]] optional<double> parse_quality(std::string_view value) noexcept
{
	value = trim_view(value);
	if( value.empty() or (value.front() != '0' and value.front() != '1') )
		return nullopt;

	const auto whole = value.front();
	value.remove_prefix(1);

	if( value.empty() )
		return whole == '1' ? 1. : 0.;

	if( value.front() != '.' )
		return nullopt;

	value.remove_prefix(1);
	if( value.size() > 3 )
		return nullopt;

	double fraction = 0;
	double divisor = 1;

	for(auto digit : value)
	{
		if( digit < '0' or digit > '9' or (whole == '1' and digit != '0') )
			return nullopt;

		fraction = fraction * 10 + (digit - '0');
		divisor *= 10;
	}
	return whole == '1' ? 1. : fraction / divisor;
}

#if RIWO_HTTP_ZLIB_SUPPORT
[[nodiscard]] error_code codec_error() noexcept {
	return make_system_error_code(std::errc::illegal_byte_sequence);
}
#else //RIWO_HTTP_ZLIB_SUPPORT
[[nodiscard]] error_code unsupported_error() noexcept {
	return make_system_error_code(std::errc::operation_not_supported);
}
#endif //RIWO_HTTP_ZLIB_SUPPORT

} //namespace

double content_coding_quality(std::string_view value, std::string_view coding) noexcept
{
	auto wanted = strtls::to_lower(strtls::trimmed(coding));
	optional<double> explicit_quality {};
	optional<double> wildcard_quality {};

	for(auto &member : string_vector::from_string(value, ','))
	{
		auto statements = string_vector::from_string(member, ';');
		if( statements.empty() )
			continue;

		auto name = strtls::to_lower(strtls::trimmed(statements.front()));
		if( name.empty() )
			continue;

		double quality = 1;
		bool valid = true;
		bool quality_seen = false;

		for(size_t i=1; i<statements.size(); i++)
		{
			auto equal = statements[i].find('=');
			if( equal == std::string::npos or
				strtls::to_lower(strtls::trimmed(statements[i].substr(0, equal))) != "q" or
				quality_seen )
			{
				valid = false;
				break;
			}
			quality_seen = true;

			auto parsed = parse_quality(statements[i].substr(equal + 1));
			if( not parsed )
			{
				valid = false;
				break;
			}
			quality = *parsed;
		}
		if( not valid )
			quality = 0;

		if( name == wanted )
			explicit_quality = quality;
		else if( name == "*" )
			wildcard_quality = quality;
	}
	return explicit_quality.value_or(wildcard_quality.value_or(0));
}

bool is_precompressed_mime_type(std::string_view value) noexcept
{
	auto type = strtls::to_lower(media_type(value));
	if( type.empty() or type == "application/octet-stream" )
		return true;

	if( (type.starts_with("image/") and type != "image/svg+xml") or
		type.starts_with("audio/") or type.starts_with("video/") )
		return true;

	if( type == "application/gzip" or type == "application/x-gzip" or
		type == "application/zip" or type == "application/x-7z-compressed" or
		type == "application/x-bzip2" or type == "application/x-xz" or
		type == "application/zstd" or type == "application/vnd.rar" or
		type == "application/x-rar-compressed" or type == "application/pdf" or
		type == "application/font-woff" or type == "font/woff" or
		type == "font/woff2" )
		return true;

	return type.starts_with("application/vnd.openxmlformats-officedocument") or
		type.starts_with("application/vnd.oasis.opendocument") or
		type.ends_with("+zip");
}

bool is_compressible_mime_type(std::string_view value) noexcept
{
	auto type = strtls::to_lower(media_type(value));
	if( is_precompressed_mime_type(type) )
		return false;

	return type.starts_with("text/") or type == "image/svg+xml" or
		   type == "application/json" or type.ends_with("+json") or
		   type == "application/xml" or type.ends_with("+xml") or
		   type == "application/javascript" or type == "application/x-javascript" or
		   type == "application/x-httpd-php" or
		   type == "application/x-www-form-urlencoded" or
		   type == "application/graphql" or type == "application/sql" or
		   type == "application/wasm" or type == "application/rtf" or
		   type == "application/yaml" or type == "application/x-yaml" or
		   type == "font/ttf" or type == "font/otf";
}

class RIWO_DECL_HIDDEN gzip_encoder::impl
{
public:
	explicit impl(int level) noexcept
	{
#if RIWO_HTTP_ZLIB_SUPPORT
		m_initialized = deflateInit2(&m_stream, level, Z_DEFLATED,
			MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK;
#else //RIWO_HTTP_ZLIB_SUPPORT
		RIWO_UNUSED(level);
#endif //RIWO_HTTP_ZLIB_SUPPORT
	}

	~impl()
	{
#if RIWO_HTTP_ZLIB_SUPPORT
		if( m_initialized )
			deflateEnd(&m_stream);
#endif //RIWO_HTTP_ZLIB_SUPPORT
	}

public:
#if RIWO_HTTP_ZLIB_SUPPORT
	z_stream m_stream {};
#endif //RIWO_HTTP_ZLIB_SUPPORT

	bool m_initialized = false;
	bool m_finished = false;
};

gzip_encoder::gzip_encoder(int level) noexcept :
	m_impl(new impl(level))
{

}

gzip_encoder::~gzip_encoder()
{
	delete m_impl;
}

gzip_encoder::gzip_encoder(gzip_encoder &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl(-1);
}

gzip_encoder &gzip_encoder::operator=(gzip_encoder &&other) noexcept
{
	if( this != &other )
	{
		delete m_impl;
		m_impl = other.m_impl;
		other.m_impl = new impl(-1);
	}
	return *this;
}

sys_expected<std::string> gzip_encoder::append(std::string_view data, bool finish) noexcept
{
#if !RIWO_HTTP_ZLIB_SUPPORT
	ignore_unused(data, finish);
	return sys_unexpected(unsupported_error());
#else //RIWO_HTTP_ZLIB_SUPPORT

	if( not m_impl->m_initialized )
		return sys_unexpected(codec_error());

	else if( m_impl->m_finished )
	{
		if( data.empty() )
			return std::string();
		return sys_unexpected(make_system_error_code(std::errc::operation_not_permitted));
	}
	std::string output;
	std::array<unsigned char,64 * 1024> buffer {};
	size_t offset = 0;
	do {
		auto input_size = static_cast<uInt>(std::min<size_t>(
			data.size() - offset, std::numeric_limits<uInt>::max()
		));
		auto last_input = offset + input_size == data.size();
		auto flush = finish and last_input ? Z_FINISH : Z_NO_FLUSH;

		m_impl->m_stream.next_in = reinterpret_cast<Bytef*>(
			const_cast<char*>(data.data() + offset)
		);
		m_impl->m_stream.avail_in = input_size;
		int result = Z_OK;
		do {
			m_impl->m_stream.next_out = buffer.data();
			m_impl->m_stream.avail_out = static_cast<uInt>(buffer.size());

			result = deflate(&m_impl->m_stream, flush);
			if( result != Z_OK and result != Z_STREAM_END )
				return sys_unexpected(codec_error());

			auto produced = buffer.size() - m_impl->m_stream.avail_out;
			output.append(reinterpret_cast<const char*>(buffer.data()), produced);
		}
		while( result != Z_STREAM_END and
			(m_impl->m_stream.avail_in != 0 or m_impl->m_stream.avail_out == 0 or flush == Z_FINISH)
		);
		offset += input_size;
		if( result == Z_STREAM_END )
		{
			m_impl->m_finished = true;
			break;
		}
	}
	while( offset < data.size() );

	if( finish and not m_impl->m_finished )
		return sys_unexpected(codec_error());
	return output;
#endif //RIWO_HTTP_ZLIB_SUPPORT
}

bool gzip_encoder::finished() const noexcept {
	return m_impl->m_finished;
}

class RIWO_DECL_HIDDEN gzip_decoder::impl
{
public:
	explicit impl(size_t max_output_size) noexcept : m_max_output_size(max_output_size)
	{
#if RIWO_HTTP_ZLIB_SUPPORT
		m_initialized = inflateInit2(&m_stream, MAX_WBITS + 16) == Z_OK;
#endif //RIWO_HTTP_ZLIB_SUPPORT
	}

	~impl()
	{
#if RIWO_HTTP_ZLIB_SUPPORT
		if( m_initialized )
			inflateEnd(&m_stream);
#endif //RIWO_HTTP_ZLIB_SUPPORT
	}

public:
#if RIWO_HTTP_ZLIB_SUPPORT
	z_stream m_stream {};
#endif //RIWO_HTTP_ZLIB_SUPPORT

	size_t m_max_output_size = 0;
	size_t m_output_size = 0;

	bool m_initialized = false;
	bool m_member_finished = false;
	bool m_finished = false;
};

gzip_decoder::gzip_decoder(size_t max_output_size) noexcept :
	m_impl(new impl(max_output_size))
{

}

gzip_decoder::~gzip_decoder()
{
	delete m_impl;
}

gzip_decoder::gzip_decoder(gzip_decoder &&other) noexcept :
	m_impl(other.m_impl)
{
	other.m_impl = new impl(default_max_decoded_body_size_v);
}

gzip_decoder &gzip_decoder::operator=(gzip_decoder &&other) noexcept
{
	if( this != &other )
	{
		delete m_impl;
		m_impl = other.m_impl;
		other.m_impl = new impl(default_max_decoded_body_size_v);
	}
	return *this;
}

sys_expected<std::string> gzip_decoder::append(std::string_view data, bool finish) noexcept
{
#if !RIWO_HTTP_ZLIB_SUPPORT
	ignore_unused(data, finish);
	return sys_unexpected(unsupported_error());
#else //RIWO_HTTP_ZLIB_SUPPORT

	if( not m_impl->m_initialized )
		return sys_unexpected(codec_error());

	else if( m_impl->m_finished )
	{
		if( data.empty() )
			return std::string();
		return sys_unexpected(codec_error());
	}
	if( data.empty() )
	{
		if( finish and m_impl->m_member_finished )
		{
			m_impl->m_finished = true;
			return std::string();
		}
		if( finish )
			return sys_unexpected(codec_error());
		return std::string();
	}
	if( m_impl->m_member_finished )
	{
		if( inflateReset2(&m_impl->m_stream, MAX_WBITS + 16) != Z_OK )
			return sys_unexpected(codec_error());
		m_impl->m_member_finished = false;
	}
	std::string output {};
	std::array<unsigned char,64 * 1024> buffer {};

	size_t offset = 0;
	while( offset < data.size() )
	{
		auto input_size = static_cast<uInt>(std::min<size_t>(
			data.size() - offset, std::numeric_limits<uInt>::max()
		));
		m_impl->m_stream.next_in = reinterpret_cast<Bytef*>(
			const_cast<char*>(data.data() + offset)
		);
		m_impl->m_stream.avail_in = input_size;
		int result = Z_OK;
		do {
			m_impl->m_stream.next_out = buffer.data();
			m_impl->m_stream.avail_out = static_cast<uInt>(buffer.size());

			result = inflate(&m_impl->m_stream, Z_NO_FLUSH);
			if( result != Z_OK and result != Z_STREAM_END )
				return sys_unexpected(codec_error());

			auto produced = buffer.size() - m_impl->m_stream.avail_out;
			if( produced > m_impl->m_max_output_size -
				std::min(m_impl->m_output_size, m_impl->m_max_output_size) )
				return sys_unexpected(make_system_error_code(std::errc::file_too_large));

			m_impl->m_output_size += produced;
			output.append(reinterpret_cast<const char*>(buffer.data()), produced);
		}
		while( result != Z_STREAM_END and
			(m_impl->m_stream.avail_in != 0 or m_impl->m_stream.avail_out == 0) );

		auto consumed = input_size - m_impl->m_stream.avail_in;
		offset += consumed;

		if( result == Z_STREAM_END )
		{
			m_impl->m_member_finished = true;
			if( offset == data.size() )
				break;

			if( inflateReset2(&m_impl->m_stream, MAX_WBITS + 16) != Z_OK )
				return sys_unexpected(codec_error());

			m_impl->m_member_finished = false;
			continue;
		}
		if( consumed == 0 )
			return sys_unexpected(codec_error());
	}
	if( finish )
	{
		if( not m_impl->m_member_finished )
			return sys_unexpected(codec_error());
		m_impl->m_finished = true;
	}
	return output;
#endif //RIWO_HTTP_ZLIB_SUPPORT
}

bool gzip_decoder::finished() const noexcept
{
	return m_impl->m_finished;
}

size_t gzip_decoder::output_size() const noexcept
{
	return m_impl->m_output_size;
}

sys_expected<std::string> gzip_compress(std::string_view data, int level) noexcept
{
	gzip_encoder encoder(level);
	return encoder.append(data, true);
}

sys_expected<std::string> gzip_decompress(std::string_view data, size_t max_output_size) noexcept
{
	gzip_decoder decoder(max_output_size);
	return decoder.append(data, true);
}

} //namespace riwo::http
