// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
#define RIWO_HTTP_EXP_CLIENT_EXCHANGE_H

#include <riwo/http_exp/body.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_client_exchange
{
	RIWO_DISABLE_COPY(basic_client_exchange)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_type = basic_body_reader<executor_type>;
	using body_reader_t = body_reader_type;
	using body_writer_type = basic_body_writer<executor_type>;
	using body_writer_t = body_writer_type;

	basic_client_exchange()
		requires std::default_initializable<executor_type> :
		basic_client_exchange(executor_type{})
	{
	}

	template <typename Exec0>
	explicit basic_client_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type> :
		executor_(get_executor_helper(std::forward<Exec0>(executor))),
		request_body_(new body_writer_type(executor_)),
		response_body_(new body_reader_type(executor_))
	{
	}

	~basic_client_exchange() = default;
	basic_client_exchange(basic_client_exchange &&) noexcept = default;
	basic_client_exchange &operator=(basic_client_exchange &&) noexcept = default;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }
	[[nodiscard]] protocol selected_protocol() const noexcept { return protocol_; }
	[[nodiscard]] body_writer_type &request_body() noexcept { return *request_body_; }

	template <typename Token = use_sync_t>
	[[nodiscard]] auto wait_response(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response_head>
	{
		return detail::unsupported_result<response_head>(
			executor_, std::forward<Token>(token));
	}

	[[nodiscard]] body_reader_type &response_body() noexcept { return *response_body_; }
	void cancel() noexcept {}
	[[nodiscard]] stream_capabilities capabilities() const noexcept { return {}; }

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept { return nullptr; }

private:
	executor_type executor_;
	protocol protocol_ = protocol::v1_1;
	std::unique_ptr<body_writer_type> request_body_;
	std::unique_ptr<body_reader_type> response_body_;
};

using client_exchange = basic_client_exchange<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
