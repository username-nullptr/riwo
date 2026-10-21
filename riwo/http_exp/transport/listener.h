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
	using result_type = basic_negotiated_transport<executor_type>;
	using accept_handler =
		asio::any_completion_handler<void(error_code,result_type)>;

	basic_transport_listener() = default;
	virtual ~basic_transport_listener() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	virtual void accept(accept_handler) = 0;
	virtual void cancel() noexcept = 0;
};

using transport_listener = basic_transport_listener<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_LISTENER_H
