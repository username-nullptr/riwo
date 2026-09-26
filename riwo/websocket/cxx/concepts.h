// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_CXX_CONCEPTS_H
#define RIWO_WEBSOCKET_CXX_CONCEPTS_H

#include <riwo/http/global.h>
#include <riwo/core/async_expected.h>

namespace riwo::websocket { namespace concepts
{

template <typename T>
concept buffer =
	riwo::concepts::buffer<T> and
	not riwo::concepts::array_buffer<T>;

} //namespace concepts

namespace core_concepts = riwo::concepts;

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_CXX_CONCEPTS_H
