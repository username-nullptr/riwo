// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "client_session.h"

namespace riwo::experimental::http
{

bool client_session::try_reserve_stream() noexcept
{
	return available_stream_capacity() != 0;
}

void client_session::release_stream(bool) noexcept
{

}

void client_session::set_release_handler(std::function<void(bool)>)
{

}

session_stream_ptr client_session::open_sync
(const request_head&, const body_spec&, error_code &error) noexcept
{
	error = make_error_code(errc::operation_not_supported);
	return {};
}

} //namespace riwo::experimental::http
