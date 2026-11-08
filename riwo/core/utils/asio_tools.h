// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_ASIO_TOOLS_H
#define RIWO_CORE_UTILS_ASIO_TOOLS_H

#include <riwo/core/utils/asio_concepts.h>
#include <riwo/core/cxx/attributes.h>

namespace riwo
{

using mutable_buffer = asio::mutable_buffer;

class RIWO_CORE_VAPI const_buffer : public asio::const_buffer
{
public:
	using asio::const_buffer::const_buffer;

	const_buffer(const const_buffer&) = default;
	const_buffer &operator=(const const_buffer&) = default;

	const_buffer(const asio::const_buffer &buf);
	const_buffer(const mutable_buffer &buf);
	const_buffer(const char *buf);

	const_buffer(const std::string &buf);
	const_buffer(std::string_view buf);

	const_buffer &operator=(const mutable_buffer &buf);
};

template <typename...Args>
[[nodiscard]] RIWO_CORE_TAPI auto buffer(Args&&...args)
	requires (sizeof...(Args) > 0);

[[nodiscard]] RIWO_CORE_TAPI
decltype(auto) get_executor_helper(concepts::sched auto &&exec);

} //namespace riwo
#include <riwo/core/utils/detail/asio_tools.h>


#endif //RIWO_CORE_UTILS_ASIO_TOOLS_H
