// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_PARSER_H
#define RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_PARSER_H

namespace riwo::http
{

optional<value> parser<protocol_model::server>::path_arg
(const core_concepts::text_p<char> auto &key) const noexcept
{
	auto it = path_args().find(strtls::to_view(key));
	return it == path_args().end() ?
		optional<value>() : riwo::make_optional(it->second);
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_SERVER_DETAIL_PARSER_H
