// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_TOOLS_CORE_BODY_NORMS_H
#define RIWO_HTTP_TOOLS_CORE_BODY_NORMS_H

#include <riwo/http/utils/file_opt_token.h>
#include <riwo/core/string_vector.h>

namespace riwo::http
{

struct basic_body_norms {};

using range_body_norms = file_range;

struct multipart_body_norms
{
	std::string boundary;
	struct package
	{
		string_vector headers;
		file_range range;
	};
	std::vector<package> packages;
};

using body_norms_t = std::variant <
	basic_body_norms, range_body_norms, multipart_body_norms
>;

} //namespace riwo::http


#endif //RIWO_HTTP_TOOLS_CORE_BODY_NORMS_H
