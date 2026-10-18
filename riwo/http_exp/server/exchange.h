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

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_server_exchange
{
	RIWO_DISABLE_COPY_MOVE(basic_server_exchange)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_type = basic_body_reader<executor_type>;
	using body_reader_t = body_reader_type;
	using body_writer_type = basic_body_writer<executor_type>;
	using body_writer_t = body_writer_type;
	using request_type = server_request;
	using request_t = request_type;

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type> :
		executor_(get_executor_helper(std::forward<Exec0>(executor))),
		request_body_(new body_reader_type(executor_)),
		response_body_(new body_writer_type(executor_))
	{
	}

	~basic_server_exchange() = default;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }
	[[nodiscard]] protocol selected_protocol() const noexcept { return protocol_; }
	[[nodiscard]] const request_type &request() const noexcept { return request_; }
	[[nodiscard]] body_reader_type &request_body() noexcept { return *request_body_; }
	[[nodiscard]] body_writer_type &response_body() noexcept { return *response_body_; }

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

	void cancel() noexcept {}
	[[nodiscard]] stream_capabilities capabilities() const noexcept { return {}; }

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept { return nullptr; }

private:
	executor_type executor_;
	protocol protocol_ = protocol::v1_1;
	request_type request_;
	std::unique_ptr<body_reader_type> request_body_;
	std::unique_ptr<body_writer_type> response_body_;
};

using server_exchange = basic_server_exchange<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_EXCHANGE_H
