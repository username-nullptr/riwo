// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_V2_DETAIL_ENGINE_H
#define RIWO_EXP_HTTP_PROTOCOL_V2_DETAIL_ENGINE_H

namespace riwo::experimental::http::v2
{

template <typename Extension>
Extension *connection_engine::extension() noexcept
{
	return static_cast<Extension*>(protocol_extension(typeid(Extension)));
}

} //namespace riwo::experimental::http::v2


#endif //RIWO_EXP_HTTP_PROTOCOL_V2_DETAIL_ENGINE_H
