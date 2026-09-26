// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "types.h"

namespace riwo::http
{

bool status::check(enumeration status, bool _throw)
{
	if( status >= 100 and status <= 599 )
	{
		switch(status)
		{
#define X_MACRO(e,v,d) case e:
		RIWO_HTTP_STATUS_TABLE
			return true;
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::status::check: Invalid http status: '{}'.",
			status
		));
	}
	return false;
}

const char *status::description(enumeration status, bool _throw)
{
	if( status >= 100 and status <= 599 )
	{
		switch(status)
		{
#define X_MACRO(e,v,d) case e: return d;
		RIWO_HTTP_STATUS_TABLE
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::status::description: Invalid http status: '{}'.",
			status
		));
	}
	return "None";
}

bool method::check(enumeration method, bool _throw)
{
	if( method > 0 )
	{
		switch(method)
		{
#define X_MACRO(e,v,d) case e:
		RIWO_HTTP_METHOD_TABLE
			return true;
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::method::check: Invalid http method: '{}'.",
			method
		));
	}
	return false;
}

const char *method::string(enumeration method, bool _throw)
{
	if( method > 0 )
	{
		switch(method)
		{
#define X_MACRO(e,v,d) case e: return d;
		RIWO_HTTP_METHOD_TABLE
#undef X_MACRO
		default: break;
		}
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::method::string: Invalid http method: '{}'.",
			method
		));
	}
	return "NONE";
}

bool redirect::check(enumeration redirect, bool _throw)
{
	switch(redirect)
	{
#define X_MACRO(e,v,d) case e:
	RIWO_HTTP_REDIRECT_TYPE_TABLE
		return true;
#undef X_MACRO
	default: break;
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::redirect::check: Invalid http redirect type: '{}'.",
			redirect
		));
	}
	return false;
}

const char *redirect::description(enumeration redirect, bool _throw)
{
	switch(redirect)
	{
#define X_MACRO(e,v,d) case e: return d;
	RIWO_HTTP_REDIRECT_TYPE_TABLE
#undef X_MACRO
	default: break;
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::redirect::string: Invalid http redirect type: '{}'.",
			redirect
		));
	}
	return "";
}

} //namespace riwo::http
