// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_ASIO_TOOLS_H
#define RIWO_CORE_UTILS_DETAIL_ASIO_TOOLS_H

namespace riwo
{

inline const_buffer::const_buffer(const asio::const_buffer &buf) :
	asio::const_buffer(buf.data(), buf.size())
{

}

inline const_buffer::const_buffer(const mutable_buffer &buf) :
	asio::const_buffer(buf.data(), buf.size())
{

}

inline const_buffer::const_buffer(const char *buf) :
	asio::const_buffer(buf, strlen(buf))
{

}

inline const_buffer::const_buffer(const std::string &buf) :
	asio::const_buffer(buf.c_str(), buf.size())
{

}

inline const_buffer::const_buffer(std::string_view buf) :
	asio::const_buffer(buf.data(), buf.size())
{

}

inline const_buffer &const_buffer::operator=(const mutable_buffer &buf)
{
	operator=(const_buffer(buf.data(), buf.size()));
	return *this;
}

template <typename...Args>
auto buffer(Args&&...args) requires (sizeof...(Args) > 0)
{
	using tuple_t = std::tuple<Args...>;
	using buf_t = std::tuple_element_t<0,tuple_t>;

	if constexpr( std::is_same_v<std::remove_cvref_t<buf_t>, std::nullptr_t> )
		return asio::buffer("",0);
	else
		return asio::buffer(std::forward<Args>(args)...);
}

decltype(auto) get_executor_helper(concepts::sched auto &&exec)
{
	using Exec = decltype(exec);
	using exec_t = std::remove_cvref_t<Exec>;

	if constexpr( is_exec_v<exec_t> )
		return std::forward<Exec>(exec);
	else
		return exec.get_executor();
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_ASIO_TOOLS_H
