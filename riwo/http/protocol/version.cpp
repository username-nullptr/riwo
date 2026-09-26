// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "version.h"

namespace riwo::http
{

bool version::check(enumeration version, bool _throw)
{
	if( version > 0 )
	{
		switch(version)
		{
#define X_MACRO(e,v,d) case e:
		RIWO_HTTP_VERSION_TABLE
			return true;
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		runtime_error::loc_throw(std::format (
			"riwo::http::version::check: Invalid http version: '{}'.",
			version
		));
	}
	return false;
}

const char *version::string(enumeration version, bool _throw)
{
	if( version > 0 )
	{
		switch(version)
		{
#define X_MACRO(e,v,d) case e: return d;
		RIWO_HTTP_VERSION_TABLE
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		runtime_error::loc_throw(std::format (
			"riwo::http::version::string: Invalid http version: '{}'.",
			version
		));
	}
	return "0.0";
}

double version::number(enumeration version, bool _throw)
{
	if( version > 0 )
	{
		switch(version)
		{
#define X_MACRO(e,v,d) case e: return static_cast<double>((v >> 8) & 0xFF) + (v & 0xFF) / 10.0;
		RIWO_HTTP_VERSION_TABLE
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		runtime_error::loc_throw(std::format (
			"riwo::http::version::number: Invalid http version: '{}'.",
			version
		));
	}
	return 0.0;
}

double version::number(bool _throw) const
{
	return number(value, _throw);
}


} //namespace riwo::http
