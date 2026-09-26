// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_HEADER_H
#define RIWO_HTTP_PROTOCOL_HEADER_H

#include <riwo/http/global.h>

namespace riwo::http
{

struct header
{
static constexpr const char
	* accept_language     = "Accept-Language"    ,
	* accept_encoding     = "Accept-Encoding"    ,
	* accept_ranges       = "Accept-Ranges"      ,
	* accept              = "Accept"             ,
	* allow               = "Allow"              ,
	* age                 = "Age"                ,
	* authorization       = "Authorization"      ,
	* content_encoding    = "Content-Encoding"   ,
	* content_disposition = "Content-Disposition",
	* content_length      = "Content-Length"     ,
	* cache_control       = "Cache-Control"      ,
	* content_range       = "Content-Range"      ,
	* content_type        = "Content-Type"       ,
	* connection          = "Connection"         ,
	* expires             = "Expires"            ,
	* expect              = "Expect"             ,
	* etag                = "ETag"               ,
	* host                = "Host"               ,
	* if_match            = "If-Match"           ,
	* if_modified_since   = "If-Modified-Since"  ,
	* if_none_match       = "If-None-Match"      ,
	* if_range            = "If-Range"           ,
	* if_unmodified_since = "If-Unmodified-Since",
	* last_modified       = "Last-Modified"      ,
	* location            = "Location"           ,
	* origin              = "Origin"             ,
	* referer             = "Referer"            ,
	* range               = "Range"              ,
	* proxy_authorization = "Proxy-Authorization",
	* trailer             = "Trailer"            ,
	* transfer_encoding   = "Transfer-Encoding"  ,
	* user_agent          = "User-Agent"         ,
	* upgrade             = "Upgrade"            ,
	* vary                = "Vary"               ;
};

using headers = map<value>;

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_HEADER_H
