// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_GENERATOR_H
#define RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_GENERATOR_H

namespace riwo::http
{

generator<protocol_model::server> &generator<protocol_model::server>::set_redirect
(core_concepts::text_p<char> auto &&url, redirect_enum type)
{
	switch(type)
	{
#define X_MACRO(e,v,d) case redirect::e : set_status(v); break;
		RIWO_HTTP_REDIRECT_TYPE_TABLE
#undef X_MACRO
	default: runtime_error::loc_throw(std::format (
			"Invalid redirect type: '{}'.", type
		));
	}
	set_header(header::location, std::forward<decltype(url)>(url));
	return *this;
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_GENERATOR_H
