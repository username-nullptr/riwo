// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_LISTENER_H
#define RIWO_HTTP_EXP_TRANSPORT_LISTENER_H

#include <riwo/http_exp/transport/negotiated_transport.h>

namespace riwo::http_exp
{

template<core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_transport_listener
{
	RIWO_DISABLE_COPY_MOVE(basic_transport_listener)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using result_t = basic_negotiated_transport<executor_t>;
	using accept_handler_t =
		asio::any_completion_handler<void(error_code,result_t)>;

	basic_transport_listener() = default;
	virtual ~basic_transport_listener() = default;
	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	virtual void accept(accept_handler_t) = 0;
	virtual void cancel() noexcept = 0;
};

using transport_listener_t = basic_transport_listener<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_LISTENER_H
