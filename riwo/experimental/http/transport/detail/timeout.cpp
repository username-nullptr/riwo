// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "timeout.h"

namespace riwo::experimental::http::detail
{

native_socket_timeout_t native_socket_timeout(connection::timeout_t value) noexcept
{
	const auto count = std::max<int64_t>(0, value.count());
#if defined(_WIN32)
	return static_cast<DWORD>(count);
#else //_WIN32
	native_socket_timeout_t result {};
	result.tv_sec = static_cast<time_t>(count / 1000);
	result.tv_usec = static_cast<suseconds_t>((count % 1000) * 1000);
	return result;
#endif //_WIN32
}

void apply_socket_timeout
(asio::ip::tcp::socket &socket, int option, optional<connection::timeout_t> value) noexcept
{
	const auto timeout = value ?
		native_socket_timeout(*value) : native_socket_timeout_t{};

	(void)::setsockopt(socket.native_handle(), SOL_SOCKET, option,
		reinterpret_cast<const char*>(&timeout), sizeof(timeout)
	);
}

[[nodiscard]] bool socket_timeout_error(const error_code &error) noexcept
{
	return error == asio::error::make_error_code(asio::error::would_block) or
		   error == asio::error::make_error_code(asio::error::try_again);
}

} //namespace riwo::experimental::http::detail
