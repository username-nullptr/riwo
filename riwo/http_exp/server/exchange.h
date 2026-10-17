// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_EXCHANGE_H

#include <riwo/http_exp/body.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>
#include <riwo/http_exp/server/request.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API server_exchange
{
public:
	using executor_type = asio::any_io_executor;

	server_exchange(const server_exchange &) = delete;
	server_exchange &operator=(const server_exchange &) = delete;
	server_exchange(server_exchange &&) = delete;
	server_exchange &operator=(server_exchange &&) = delete;
	~server_exchange();

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] const server_request &request() const noexcept;
	[[nodiscard]] body_reader &request_body() noexcept;
	[[nodiscard]] body_writer &response_body() noexcept;

	template <typename Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>
	{
		return detail::unsupported_operation(executor_, std::forward<Token>(token));
	}

	template <typename Body, typename Token = use_sync_t>
	auto respond(response_head, Body &&, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>
	{
		return detail::unsupported_operation(executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	auto inform(response_head, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>
	{
		return detail::unsupported_operation(executor_, std::forward<Token>(token));
	}

	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept { return nullptr; }

private:
	friend class server_session_fixture_access;
	explicit server_exchange(executor_type executor = {});
	executor_type executor_;
	protocol protocol_ = protocol::v1_1;
	server_request request_;
	std::unique_ptr<body_reader> request_body_;
	std::unique_ptr<body_writer> response_body_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_EXCHANGE_H
