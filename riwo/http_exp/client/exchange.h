// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
#define RIWO_HTTP_EXP_CLIENT_EXCHANGE_H

#include <riwo/http_exp/body.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API client_exchange
{
public:
	using executor_type = asio::any_io_executor;

	client_exchange();
	~client_exchange();
	client_exchange(const client_exchange &) = delete;
	client_exchange &operator=(const client_exchange &) = delete;
	client_exchange(client_exchange &&) noexcept;
	client_exchange &operator=(client_exchange &&) noexcept;

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] body_writer &request_body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto wait_response(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response_head>
	{
		return detail::unsupported_result<response_head>(
			executor_, std::forward<Token>(token));
	}

	[[nodiscard]] body_reader &response_body() noexcept;
	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept { return nullptr; }

private:
	executor_type executor_ {};
	protocol protocol_ = protocol::v1_1;
	std::unique_ptr<body_writer> request_body_;
	std::unique_ptr<body_reader> response_body_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
