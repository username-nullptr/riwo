// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_UTILITIES_H
#define RIWO_CORE_UTILS_DETAIL_UTILITIES_H

#include <riwo/core/utils/string_tools.h>

namespace riwo
{

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::basic_endpoint_wrapper
(const concepts::any_string_p auto &address, uint16_t port) :
	value(asio::ip::make_address(strtls::to_view(address)), port)
{

}

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::basic_endpoint_wrapper
(const concepts::any_string_p auto &address) :
	basic_endpoint_wrapper(std::forward<decltype(address)>(address), 0)
{

}

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::basic_endpoint_wrapper(ip_type type, uint16_t port)
{
	if( type == ip_type::v4 )
		value = endpoint_t(asio::ip::address_v4::any(), port);
	else if( type == ip_type::v6 )
		value = endpoint_t(asio::ip::address_v6::any(), port);
	else
		value = endpoint_t(asio::ip::address_v4::loopback(), port);
}

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::basic_endpoint_wrapper(ip_type type) :
	basic_endpoint_wrapper(type, 0)
{
}

template <typename Protocol>
template <typename...Args>
basic_endpoint_wrapper<Protocol>::basic_endpoint_wrapper(Args&&...args) requires
	concepts::constructible<endpoint_t,Args&&...>
{
	if constexpr( sizeof...(Args) == 2 )
	{
		auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
		value = endpoint_t(std::get<0>(tuple), static_cast<uint16_t>(std::get<1>(tuple)));
	}
	else
		value = endpoint_t(std::forward<Args>(args)...);
}

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::operator endpoint_t&()
{
	return value;
}

template <typename Protocol>
basic_endpoint_wrapper<Protocol>::operator const endpoint_t&() const
{
	return value;
}

template <typename Protocol>
typename basic_endpoint_wrapper<Protocol>::endpoint_t &basic_endpoint_wrapper<Protocol>::operator*()
{
	return value;
}

template <typename Protocol>
typename basic_endpoint_wrapper<Protocol>::endpoint_t *basic_endpoint_wrapper<Protocol>::operator->()
{
	return &value;
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_UTILITIES_H
