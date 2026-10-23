// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "compression.h"

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
# include <zlib.h>
#endif

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] std::string_view trim(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

[[nodiscard]] std::string lower(std::string_view value)
{
	std::string result(value);
	std::ranges::transform(result, result.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return result;
}

[[nodiscard]] optional<double> quality(std::string_view value) noexcept
{
	value = trim(value);
	if( value.empty() or (value.front() != '0' and value.front() != '1') )
		return {};
	const auto whole = value.front();
	value.remove_prefix(1);
	if( value.empty() )
		return whole == '1' ? 1. : 0.;
	if( value.front() != '.' )
		return {};
	value.remove_prefix(1);
	if( value.size() > 3 )
		return {};
	double fraction = 0;
	double divisor = 1;
	for( const auto digit : value )
	{
		if( digit < '0' or digit > '9' or (whole == '1' and digit != '0') )
			return {};
		fraction = fraction * 10 + (digit - '0');
		divisor *= 10;
	}
	return whole == '1' ? 1. : fraction / divisor;
}

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
[[nodiscard]] error_code codec_error() noexcept {
	return make_system_error_code(std::errc::illegal_byte_sequence);
}
#else
[[nodiscard]] error_code unsupported_error() noexcept {
	return make_system_error_code(std::errc::operation_not_supported);
}
#endif

} //namespace

double content_coding_quality(std::string_view value,
	std::string_view coding) noexcept
{
	try
	{
		const auto wanted = lower(trim(coding));
		optional<double> explicit_value;
		optional<double> wildcard;
		size_t begin = 0;
		while( begin <= value.size() )
		{
			const auto comma = value.find(',', begin);
			auto item = value.substr(begin, comma == std::string_view::npos ?
				value.size() - begin : comma - begin);
			const auto semicolon = item.find(';');
			const auto name = lower(trim(item.substr(0, semicolon)));
			double result = 1.;
			bool valid = not name.empty();
			if( semicolon != std::string_view::npos )
			{
				auto parameter = trim(item.substr(semicolon + 1));
				const auto equal = parameter.find('=');
				if( equal == std::string_view::npos or
					lower(trim(parameter.substr(0, equal))) != "q" )
					valid = false;
				else if( auto parsed = quality(parameter.substr(equal + 1)) )
					result = *parsed;
				else
					valid = false;
			}
			if( not valid )
				result = 0.;
			if( name == wanted )
				explicit_value = result;
			else if( name == "*" )
				wildcard = result;
			if( comma == std::string_view::npos )
				break;
			begin = comma + 1;
		}
		return explicit_value.value_or(wildcard.value_or(0.));
	}
	catch(...) {
		return 0.;
	}
}

bool is_precompressed_mime_type(std::string_view value) noexcept
{
	try
	{
		const auto type = lower(trim(value.substr(0, value.find(';'))));
		if( type.empty() or type == "application/octet-stream" )
			return true;
		if( (type.starts_with("image/") and type != "image/svg+xml") or
			type.starts_with("audio/") or type.starts_with("video/") )
			return true;
		return type == "application/gzip" or type == "application/zip" or
			type == "application/pdf" or type == "font/woff" or
			type == "font/woff2" or type.ends_with("+zip");
	}
	catch(...) {
		return true;
	}
}

bool is_compressible_mime_type(std::string_view value) noexcept
{
	try
	{
		const auto type = lower(trim(value.substr(0, value.find(';'))));
		if( is_precompressed_mime_type(type) )
			return false;
		return type.starts_with("text/") or type == "image/svg+xml" or
			type == "application/json" or type.ends_with("+json") or
			type == "application/xml" or type.ends_with("+xml") or
			type == "application/javascript" or type == "application/wasm" or
			type == "application/x-www-form-urlencoded";
	}
	catch(...) {
		return false;
	}
}

class gzip_encoder::impl
{
public:
	explicit impl(int level) noexcept
	{
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		initialized = deflateInit2(&stream, level, Z_DEFLATED,
			MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK;
#else
		ignore_unused(level);
#endif
	}
	~impl()
	{
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		if( initialized )
			deflateEnd(&stream);
#endif
	}
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
	z_stream stream {};
#endif
	bool initialized = false;
	bool ended = false;
};

gzip_encoder::gzip_encoder(int level) noexcept :
	m_impl(std::make_unique<impl>(level)) {}
gzip_encoder::~gzip_encoder() = default;
gzip_encoder::gzip_encoder(gzip_encoder &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(-1);
}

gzip_encoder &gzip_encoder::operator=(gzip_encoder &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(-1);
	return *this;
}

sys_expected<std::string> gzip_encoder::append(std::string_view data,
	bool finish) noexcept
{
#if !RIWO_HTTP_EXP_ZLIB_SUPPORT
	ignore_unused(data, finish);
	return sys_unexpected(unsupported_error());
#else
	if( not m_impl->initialized )
		return sys_unexpected(codec_error());
	if( m_impl->ended )
		return data.empty() ? sys_expected<std::string>("") :
		sys_expected<std::string>(sys_unexpected(
			make_system_error_code(std::errc::operation_not_permitted)));
	std::string output;
	std::array<unsigned char,16384> buffer {};
	size_t offset = 0;
	bool first = true;
	do
	{
		first = false;
		const auto input_size = static_cast<uInt>(std::min<size_t>(
			data.size() - offset, std::numeric_limits<uInt>::max()));
		m_impl->stream.next_in = reinterpret_cast<Bytef*>(
			const_cast<char*>(data.data() + offset));
		m_impl->stream.avail_in = input_size;
		const auto flush = finish and offset + input_size == data.size() ?
			Z_FINISH : Z_NO_FLUSH;
		int result = Z_OK;
		do
		{
			m_impl->stream.next_out = buffer.data();
			m_impl->stream.avail_out = static_cast<uInt>(buffer.size());
			result = deflate(&m_impl->stream, flush);
			if( result != Z_OK and result != Z_STREAM_END )
				return sys_unexpected(codec_error());
			output.append(reinterpret_cast<const char*>(buffer.data()),
				buffer.size() - m_impl->stream.avail_out);
		}
		while( result != Z_STREAM_END and (m_impl->stream.avail_in != 0 or
			m_impl->stream.avail_out == 0 or flush == Z_FINISH) );
		offset += input_size;
		if( result == Z_STREAM_END )
			m_impl->ended = true;
	}
	while( offset < data.size() or (first and data.empty()) );
	if( finish and not m_impl->ended )
		return sys_unexpected(codec_error());
	return output;
#endif
}

bool gzip_encoder::finished() const noexcept { return m_impl->ended; }

class gzip_decoder::impl
{
public:
	explicit impl(size_t limit) noexcept : max_output(limit)
	{
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		initialized = inflateInit2(&stream, MAX_WBITS + 16) == Z_OK;
#endif
	}
	~impl()
	{
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		if( initialized )
			inflateEnd(&stream);
#endif
	}
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
	z_stream stream {};
#endif
	size_t max_output = 0;
	size_t output = 0;
	bool initialized = false;
	bool member_ended = false;
	bool ended = false;
};

gzip_decoder::gzip_decoder(size_t max_output_size) noexcept :
	m_impl(std::make_unique<impl>(max_output_size)) {}
gzip_decoder::~gzip_decoder() = default;
gzip_decoder::gzip_decoder(gzip_decoder &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(default_max_decoded_body_size_v);
}

gzip_decoder &gzip_decoder::operator=(gzip_decoder &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(default_max_decoded_body_size_v);
	return *this;
}

sys_expected<std::string> gzip_decoder::append(std::string_view data,
	bool finish) noexcept
{
#if !RIWO_HTTP_EXP_ZLIB_SUPPORT
	ignore_unused(data, finish);
	return sys_unexpected(unsupported_error());
#else
	if( not m_impl->initialized or m_impl->ended )
		return sys_unexpected(codec_error());
	if( data.empty() )
	{
		if( finish and m_impl->member_ended )
		{
			m_impl->ended = true;
			return std::string{};
		}
		return finish ? sys_expected<std::string>(sys_unexpected(codec_error())) :
			sys_expected<std::string>(std::string{});
	}
	if( m_impl->member_ended )
	{
		if( inflateReset2(&m_impl->stream, MAX_WBITS + 16) != Z_OK )
			return sys_unexpected(codec_error());
		m_impl->member_ended = false;
	}
	std::string output;
	std::array<unsigned char,16384> buffer {};
	size_t offset = 0;
	while( offset < data.size() )
	{
		const auto input_size = static_cast<uInt>(std::min<size_t>(
			data.size() - offset, std::numeric_limits<uInt>::max()));
		m_impl->stream.next_in = reinterpret_cast<Bytef*>(
			const_cast<char*>(data.data() + offset));
		m_impl->stream.avail_in = input_size;
		int result = Z_OK;
		do
		{
			m_impl->stream.next_out = buffer.data();
			m_impl->stream.avail_out = static_cast<uInt>(buffer.size());
			result = inflate(&m_impl->stream, Z_NO_FLUSH);
			if( result != Z_OK and result != Z_STREAM_END )
				return sys_unexpected(codec_error());
			const auto produced = buffer.size() - m_impl->stream.avail_out;
			if( produced > m_impl->max_output -
				std::min(m_impl->output, m_impl->max_output) )
				return sys_unexpected(make_system_error_code(
					std::errc::file_too_large));
			m_impl->output += produced;
			output.append(reinterpret_cast<const char*>(buffer.data()), produced);
		}
		while( result != Z_STREAM_END and (m_impl->stream.avail_in != 0 or
			m_impl->stream.avail_out == 0) );
		const auto consumed = input_size - m_impl->stream.avail_in;
		offset += consumed;
		if( result == Z_STREAM_END )
		{
			m_impl->member_ended = true;
			if( offset != data.size() )
			{
				if( inflateReset2(&m_impl->stream, MAX_WBITS + 16) != Z_OK )
					return sys_unexpected(codec_error());
				m_impl->member_ended = false;
			}
		}
		else if( consumed == 0 )
			return sys_unexpected(codec_error());
	}
	if( finish )
	{
		if( not m_impl->member_ended )
			return sys_unexpected(codec_error());
		m_impl->ended = true;
	}
	return output;
#endif
}

bool gzip_decoder::finished() const noexcept { return m_impl->ended; }
size_t gzip_decoder::output_size() const noexcept { return m_impl->output; }

sys_expected<std::string> gzip_compress(std::string_view data,
	int level) noexcept
{
	gzip_encoder encoder(level);
	return encoder.append(data, true);
}

sys_expected<std::string> gzip_decompress(std::string_view data,
	size_t max_output_size) noexcept
{
	gzip_decoder decoder(max_output_size);
	return decoder.append(data, true);
}

} //namespace riwo::http_exp
