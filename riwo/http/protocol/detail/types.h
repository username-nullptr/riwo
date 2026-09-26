// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_DETAIL_TYPES_H
#define RIWO_HTTP_PROTOCOL_DETAIL_TYPES_H

namespace riwo::http
{

template <status_enum Status>
consteval bool status::is_valid()
{
	if constexpr( Status >= 100 and Status <= 599 )
	{
#define X_MACRO(e,v,d) if constexpr( Status == e ) return true;
		RIWO_HTTP_STATUS_TABLE
#undef X_MACRO
		else return false;
	}
	else return false;
}

template <status_enum Status>
consteval const char *status::description() requires is_valid_v<Status>
{
#define X_MACRO(e,v,d) if constexpr( Status == e ) return d;
	RIWO_HTTP_STATUS_TABLE
#undef X_MACRO
	else return "None";
}

template <method_enum Method>
consteval bool method::is_valid()
{
	if constexpr( Method > 0 )
	{
#define X_MACRO(e,v,d) if constexpr( Method == e ) return true;
		RIWO_HTTP_METHOD_TABLE
		else return false;
#undef X_MACRO
	}
	else return false;
}

template <method_enum Method>
consteval const char *method::string() requires is_valid_v<Method>
{
	if constexpr( Method > 0 )
	{
#define X_MACRO(e,v,d) if constexpr( Method == e ) return d;
		RIWO_HTTP_METHOD_TABLE
		else return "";
#undef X_MACRO
	}
	else return "";
}

constexpr method_enum method::from_string(std::string_view str, bool _throw)
{
	if( str != "NONE" )
	{
#define X_MACRO(e,v,d) if( str == d ) return method::e;
		RIWO_HTTP_METHOD_TABLE
#undef X_MACRO
	}
	if( _throw )
	{
		invalid_argument::loc_throw(std::format (
			"riwo::http::method::from_string: Invalid http method: '{}'.", str
		));
	}
	return none;
}

constexpr method::method(std::string_view str) :
	value(from_string(str, true))
{

}

template <redirect_enum Redirect>
consteval bool redirect::is_valid()
{
#define X_MACRO(e,v,d) if constexpr( Redirect == e ) return true;
	RIWO_HTTP_REDIRECT_TYPE_TABLE
#undef X_MACRO
	else return false;
}

template <redirect_enum Redirect>
consteval const char *redirect::description() requires is_valid_v<Redirect>
{
#define X_MACRO(e,v,d) if constexpr( Redirect == e ) return d;
	RIWO_HTTP_REDIRECT_TYPE_TABLE
#undef X_MACRO
	else return "";
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_DETAIL_TYPES_H
