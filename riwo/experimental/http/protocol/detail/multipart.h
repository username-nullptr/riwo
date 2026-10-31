// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_H
#define RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_H

#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http
{

multipart_form::multipart_form(core_concepts::text_p<char> auto &&boundary)
{
	initialize_boundary (
		strtls::to_string(std::forward<decltype(boundary)>(boundary))
	);
}

multipart_form &multipart_form::add_field
(core_concepts::text_p<char> auto &&name, core_concepts::text_p<char> auto &&value)
{
	add_field_impl (
		strtls::to_string(std::forward<decltype(name)>(name)),
		strtls::to_string(std::forward<decltype(value)>(value))
	);
	return *this;
}

multipart_form &multipart_form::add_file(core_concepts::text_p<char> auto &&name,
	core_concepts::text_p<char> auto &&filename, core_concepts::text_p<char> auto &&data)
{
	add_file (
		std::forward<decltype(name)>(name),
		std::forward<decltype(filename)>(filename),
		std::forward<decltype(data)>(data),
		"application/octet-stream"
	);
	return *this;
}

multipart_form &multipart_form::add_file
(core_concepts::text_p<char> auto &&name, core_concepts::text_p<char> auto &&filename,
	core_concepts::text_p<char> auto &&data, core_concepts::text_p<char> auto &&content_type)
{
	add_file_impl (
		strtls::to_string(std::forward<decltype(name)>(name)),
		strtls::to_string(std::forward<decltype(filename)>(filename)),
		strtls::to_string(std::forward<decltype(data)>(data)),
		strtls::to_string(std::forward<decltype(content_type)>(content_type))
	);
	return *this;
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_DETAIL_MULTIPART_H
