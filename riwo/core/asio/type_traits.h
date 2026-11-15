// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ASIO_TYPE_TRAITS_H
#define RIWO_CORE_ASIO_TYPE_TRAITS_H

#include <riwo/core/asio/asio.h>

namespace riwo
{

#if RIWO_USING_BOOST_ASIO

using error_code = boost::system::error_code;
using error_category_t = boost::system::error_category;
using boost::system::system_category;

#else //RIWO_USING_BOOST_ASIO

using error_code = std::error_code;
using error_category_t = std::error_category;
using std::system_category;

#endif //RIWO_USING_BOOST_ASIO

[[nodiscard]] inline error_code make_system_error_code(std::errc value) noexcept {
	return { std::make_error_code(value) };
}
namespace errc = asio::error;

} //namespace riwo


#endif //RIWO_CORE_ASIO_TYPE_TRAITS_H
