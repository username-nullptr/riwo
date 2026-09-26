// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_UTILITIES_H
#define RIWO_CORE_UTILS_UTILITIES_H

#include <riwo/core/utils/asio_concepts.h>
#include <riwo/core/utils/streamer.h>

#include <riwo/core/cxx/string_concepts.h>
#include <riwo/core/cxx/attributes.h>

namespace riwo
{

enum class ip_type {
	v4, v6, loopback
};

template <typename Protocol>
struct RIWO_CORE_TAPI basic_endpoint_wrapper
{
	using protocol_t = Protocol;
	using endpoint_t = asio::ip::basic_endpoint<protocol_t>;
	endpoint_t value;

	basic_endpoint_wrapper() = default;
	basic_endpoint_wrapper(const concepts::any_string_p auto &address, uint16_t port);
	basic_endpoint_wrapper(const concepts::any_string_p auto &address);

	basic_endpoint_wrapper(ip_type type, uint16_t port);
	basic_endpoint_wrapper(ip_type type);

	template <typename...Args>
	basic_endpoint_wrapper(Args&&...args) requires
		concepts::constructible<endpoint_t,Args&&...>;

	operator endpoint_t&();
	operator const endpoint_t&() const;

	endpoint_t &operator*();
	endpoint_t *operator->();
};

using tcp_endpoint_wrapper = basic_endpoint_wrapper<asio::ip::tcp>;
using udp_endpoint_wrapper = basic_endpoint_wrapper<asio::ip::udp>;

} //namespace riwo
#include <riwo/core/utils/detail/utils.h>


#endif //RIWO_CORE_UTILS_UTILITIES_H
