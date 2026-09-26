// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_DETAIL_PATH_OPT_TOKEN_H
#define RIWO_HTTP_UTILS_DETAIL_PATH_OPT_TOKEN_H

namespace riwo::http
{

template <core_concepts::character CharT>
template <core_concepts::string_p<CharT> Str>
basic_path_opt_token<CharT>::basic_path_opt_token(Str &&path) :
	paths{std::forward<Str>(path)}
{

}

template <core_concepts::character CharT>
template <core_concepts::string<CharT> Str>
basic_path_opt_token<CharT>::basic_path_opt_token(std::vector<Str> &&path_values) :
	paths{std::forward<std::vector<Str>>(path_values)}
{

}

template <core_concepts::character CharT>
template <core_concepts::string<CharT> Str>
basic_path_opt_token<CharT>::basic_path_opt_token(std::initializer_list<Str> path_values)
{
	for(auto &path : path_values)
		this->paths.emplace_back(path);
}

template <core_concepts::character CharT>
template <core_concepts::string_p<CharT>...Str>
basic_path_opt_token<CharT>::basic_path_opt_token(Str&&...path_args)
{
	(void) std::initializer_list<int> {
		(this->paths.emplace_back(std::forward<Str>(path_args)), 0) ...
	};
}

} //namespace riwo::http


#endif //RIWO_HTTP_UTILS_DETAIL_PATH_OPT_TOKEN_H
