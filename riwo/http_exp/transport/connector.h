// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_CONNECTOR_H
#define RIWO_HTTP_EXP_TRANSPORT_CONNECTOR_H

#include <riwo/http_exp/protocol/policy.h>
#include <riwo/http_exp/transport/connect_options.h>
#include <riwo/http_exp/transport/negotiated_transport.h>

namespace riwo::http_exp
{

struct connect_request
{
	std::string scheme {};
	std::string host {};
	uint16_t port = 0;
	protocol_policy protocols {};
	proxy_policy proxy {};
	tls_policy tls {};
	milliseconds deadline {30000};
	optional<std::string> local_bind {};
};

template<core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_transport_connector
{
	RIWO_DISABLE_COPY_MOVE(basic_transport_connector)

public:
	using executor_type = Exec;
	using result_type = basic_negotiated_transport<executor_type>;
	using connect_handler =
		asio::any_completion_handler<void(error_code,result_type)>;

	basic_transport_connector() = default;
	virtual ~basic_transport_connector() = default;
	[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
	[[nodiscard]] virtual sys_expected<result_type>
	connect_sync(const connect_request &) noexcept
	{
		return sys_unexpected(make_error_code(errc::operation_not_supported));
	}
	virtual void connect(connect_request, connect_handler) = 0;
	virtual void cancel() noexcept = 0;
};

using transport_connector = basic_transport_connector<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_CONNECTOR_H
