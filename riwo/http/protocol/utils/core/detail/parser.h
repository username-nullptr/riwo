// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_PARSER_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_PARSER_H

namespace riwo::http
{

template <typename Opt>
auto parser<protocol_model::base>::make_file_opt_token(Opt &&opt)
	noexcept requires file_opt_token_v<Opt>
{
	using opt_t = std::remove_cvref_t<Opt>;
	if constexpr( is_any_string_v<opt_t> or std::same_as<opt_t,std::filesystem::path> or
		is_fstream_v<opt_t,char> or is_ofstream_v<opt_t,char> )
	{
		auto token = http::make_file_opt_token(std::forward<Opt>(opt));
		using token_t = decltype(token);

		auto expected = token.init(std::ios::out | std::ios::binary);
		if( expected )
			return sys_expected<token_t>(std::move(token));
		return sys_expected<token_t>(sys_unexpected(expected.error()));
	}
	else
	{
		auto expected = opt.init(std::ios::out | std::ios::binary);
		if( expected )
			return sys_expected<opt_t>(std::forward<Opt>(opt));
		return sys_expected<opt_t>(sys_unexpected(expected.error()));
	}
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_PARSER_H
