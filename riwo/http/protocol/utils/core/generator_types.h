// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_GENERATOR_TYPES_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_GENERATOR_TYPES_H

#include <riwo/http/protocol/types.h>

namespace riwo::http
{

template <protocol_model>
class generator {};

enum class generator_state {
	header, content_length, chunk, finish
};

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_GENERATOR_TYPES_H
