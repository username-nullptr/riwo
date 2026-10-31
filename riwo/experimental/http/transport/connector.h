// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TRANSPORT_CONNECTOR_H
#define RIWO_EXP_HTTP_TRANSPORT_CONNECTOR_H

#include <riwo/experimental/http/protocol/policy.h>
#include <riwo/experimental/http/transport/connect_options.h>
#include <riwo/experimental/http/transport/negotiated_transport.h>

namespace riwo::experimental::http
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
class RIWO_EXP_HTTP_TAPI basic_transport_connector
{
	RIWO_DISABLE_COPY_MOVE(basic_transport_connector)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using result_t = basic_negotiated_transport<executor_t>;
	using connect_handler_t = asio::any_completion_handler<void(error_code,result_t)>;

public:
	basic_transport_connector() = default;
	virtual ~basic_transport_connector() = default;

	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	[[nodiscard]] virtual sys_expected<result_t> connect_sync(const connect_request&) noexcept;

	virtual void connect(connect_request, connect_handler_t) = 0;
	virtual void cancel() noexcept = 0;
};

using transport_connector = basic_transport_connector<>;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/transport/detail/connector.h>


#endif //RIWO_EXP_HTTP_TRANSPORT_CONNECTOR_H
