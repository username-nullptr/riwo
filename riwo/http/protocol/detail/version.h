// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_DETAIL_VERSION_H
#define RIWO_HTTP_PROTOCOL_DETAIL_VERSION_H

namespace riwo::http
{

template <version_enum Version>
consteval bool version::is_valid()
{
	if constexpr( Version > 0 )
	{
#define X_MACRO(e,v,d) if constexpr( Version == e ) return true;
		RIWO_HTTP_VERSION_TABLE
		else return false;
#undef X_MACRO
	}
	else return false;
}

template <version_enum Version>
consteval const char *version::string() requires is_valid_v<Version>
{
#define X_MACRO(e,v,d) if constexpr( Version == e ) return d;
	RIWO_HTTP_VERSION_TABLE
#undef X_MACRO
	else return "0.0";
}

constexpr version_enum version::from_string(std::string_view str)
{
	if( str != "0.0" )
	{
#define X_MACRO(e,v,d) if( str == d ) return version_enum::e;
		RIWO_HTTP_VERSION_TABLE
#undef X_MACRO
	}
	runtime_error::loc_throw(std::format (
		"riwo::http::version::from_string: Invalid http version string: '{}'.", str
	));
}

constexpr version::version(std::string_view str) :
	value(from_string(str))
{

}

template <version_enum Version>
consteval double version::number() requires is_valid_v<Version>
{
#define X_MACRO(e,v,d) \
	if constexpr( Version == e ) \
		return static_cast<double>((v >> 8) & 0xFF) + (v & 0xFF) / 10.0;
	RIWO_HTTP_VERSION_TABLE
#undef X_MACRO
	else return 0.0;
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_DETAIL_VERSION_H
