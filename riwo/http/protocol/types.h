// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_TYPES_H
#define RIWO_HTTP_PROTOCOL_TYPES_H

#include <riwo/core/utils/flags.h>
#include <riwo/http/protocol/version.h>
#include <riwo/http/protocol/header.h>
#include <riwo/http/protocol/cookie.h>
#include <riwo/http/protocol/model.h>

namespace riwo::http
{

#define RIWO_HTTP_STATUS_TABLE \
X_MACRO( none                            ,   0 , "None"                            ) \
X_MACRO( continue_upload                 , 100 , "Continue"                        ) \
X_MACRO( switching_protocols             , 101 , "Switching Protocols"             ) \
X_MACRO( processing                      , 102 , "Processing"                      ) \
X_MACRO( ok                              , 200 , "OK"                              ) \
X_MACRO( created                         , 201 , "Created"                         ) \
X_MACRO( accepted                        , 202 , "Accepted"                        ) \
X_MACRO( non_authoritative_information   , 203 , "Non-Authoritative Information"   ) \
X_MACRO( no_content                      , 204 , "No Content"                      ) \
X_MACRO( reset_content                   , 205 , "Reset Content"                   ) \
X_MACRO( partial_content                 , 206 , "Partial Content"                 ) \
X_MACRO( multi_status                    , 207 , "Multi-Status"                    ) \
X_MACRO( already_reported                , 208 , "Already Reported"                ) \
X_MACRO( im_used                         , 226 , "IM Used"                         ) \
X_MACRO( multiple_choices                , 300 , "Multiple Choices"                ) \
X_MACRO( moved_permanently               , 301 , "Moved Permanently"               ) \
X_MACRO( found                           , 302 , "Found"                           ) \
X_MACRO( see_other                       , 303 , "See Other"                       ) \
X_MACRO( not_modified                    , 304 , "Not Modified"                    ) \
X_MACRO( use_proxy                       , 305 , "Use Proxy"                       ) \
X_MACRO( temporary_redirect              , 307 , "Temporary Redirect"              ) \
X_MACRO( permanent_redirect              , 308 , "Permanent Redirect"              ) \
X_MACRO( bad_request                     , 400 , "Bad Request"                     ) \
X_MACRO( unauthorized                    , 401 , "Unauthorized"                    ) \
X_MACRO( payment_required                , 402 , "Payment Required"                ) \
X_MACRO( forbidden                       , 403 , "Forbidden"                       ) \
X_MACRO( not_found                       , 404 , "Not Found"                       ) \
X_MACRO( method_not_allowed              , 405 , "Method Not Allowed"              ) \
X_MACRO( not_acceptable                  , 406 , "Not Acceptable"                  ) \
X_MACRO( proxy_authentication_required   , 407 , "Proxy Authentication Required"   ) \
X_MACRO( request_timeout                 , 408 , "Request Timeout"                 ) \
X_MACRO( conflict                        , 409 , "Conflict"                        ) \
X_MACRO( gone                            , 410 , "Gone"                            ) \
X_MACRO( length_required                 , 411 , "Length Required"                 ) \
X_MACRO( precondition_failed             , 412 , "Precondition Failed"             ) \
X_MACRO( payload_too_large               , 413 , "Payload Too Large"               ) \
X_MACRO( uri_too_long                    , 414 , "URI Too Long"                    ) \
X_MACRO( unsupported_media_type          , 415 , "Unsupported Media Type"          ) \
X_MACRO( range_not_satisfiable           , 416 , "Range Not Satisfiable"           ) \
X_MACRO( expectation_failed              , 417 , "Expectation Failed"              ) \
X_MACRO( misdirected_request             , 421 , "Misdirected Request"             ) \
X_MACRO( unprocessable_entity            , 422 , "Unprocessable Entity"            ) \
X_MACRO( locked                          , 423 , "Locked"                          ) \
X_MACRO( failed_dependency               , 424 , "Failed Dependency"               ) \
X_MACRO( upgrade_required                , 426 , "Upgrade Required"                ) \
X_MACRO( precondition_required           , 428 , "Precondition Required"           ) \
X_MACRO( tooMany_requests                , 429 , "Too Many Requests"               ) \
X_MACRO( request_header_fields_too_large , 431 , "Request Header Fields Too Large" ) \
X_MACRO( unavailable_for_legal_reasons   , 451 , "Unavailable For Legal Reasons"   ) \
X_MACRO( internal_server_error           , 500 , "Internal Server Error"           ) \
X_MACRO( not_implemented                 , 501 , "Not Implemented"                 ) \
X_MACRO( bad_gateway                     , 502 , "Bad Gateway"                     ) \
X_MACRO( service_unavailable             , 503 , "Service Unavailable"             ) \
X_MACRO( gateway_timeout                 , 504 , "Gateway Timeout"                 ) \
X_MACRO( http_version_not_supported      , 505 , "HTTP Version Not Supported"      ) \
X_MACRO( variant_also_negotiates         , 506 , "Variant Also Negotiates"         ) \
X_MACRO( insufficient_storage            , 507 , "Insufficient Storage"            ) \
X_MACRO( loop_detected                   , 508 , "Loop Detected"                   ) \
X_MACRO( not_extended                    , 510 , "Not Extended"                    ) \
X_MACRO( network_authentication_required , 511 , "Network Authentication Required" )

#define X_MACRO(e,v,d) e = (v),
RIWO_HTTP_DEFINE_ENUM(uint32_t, status, RIWO_HTTP_STATUS_TABLE, description);
#undef X_MACRO

#define RIWO_HTTP_METHOD_TABLE \
X_MACRO( none    , 0x0000 , "NONE"    ) \
X_MACRO( get     , 0x0001 , "GET"     ) \
X_MACRO( put     , 0x0002 , "PUT"     ) \
X_MACRO( post    , 0x0004 , "POST"    ) \
X_MACRO( head    , 0x0008 , "HEAD"    ) \
X_MACRO( patch   , 0x0010 , "PATCH"   ) \
X_MACRO( delet   , 0x0020 , "DELETE"  ) \
X_MACRO( options , 0x0040 , "OPTIONS" ) \
X_MACRO( trace   , 0x0080 , "TRACE"   ) \
X_MACRO( connect , 0x0100 , "CONNECT" )

#define X_MACRO(e,v,d) e = (v),
RIWO_HTTP_DEFINE_ENUM(uint16_t, method, RIWO_HTTP_METHOD_TABLE, string,
	[[nodiscard]] static constexpr enumeration from_string(std::string_view str, bool _throw = false);
	constexpr method(std::string_view str);
);
#undef X_MACRO
RIWO_DECLARE_FLAGS(methods, method_enum);

#define RIWO_HTTP_REDIRECT_TYPE_TABLE \
X_MACRO( moved_permanently  , status::moved_permanently  , "Moved Permanently"  ) \
X_MACRO( permanent_redirect , status::permanent_redirect , "Permanent Redirect" ) \
X_MACRO( found              , status::found              , "Found"              ) \
X_MACRO( see_other          , status::see_other          , "See Other"          ) \
X_MACRO( temporary_redirect , status::temporary_redirect , "Temporary Redirect" ) \
X_MACRO( multiple_choices   , status::multiple_choices   , "Multiple Choices"   ) \
X_MACRO( not_modified       , status::not_modified       , "Not Modified"       )

#define X_MACRO(e,v,d) e = (v),
RIWO_HTTP_DEFINE_ENUM(uint32_t, redirect, RIWO_HTTP_REDIRECT_TYPE_TABLE, description);
#undef X_MACRO

enum class request_target_form {
	origin, absolute, authority, asterisk
};
using parameters = parameter_map;

} //namespace riwo::http
#include <riwo/http/protocol/detail/types.h>


#endif //RIWO_HTTP_PROTOCOL_TYPES_H
