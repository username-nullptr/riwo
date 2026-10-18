// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_EXCHANGE_H

#include <riwo/http_exp/body.h>
#include <riwo/http_exp/message/request_head.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp::detail
{
class server_exchange_state;
class stream;
}

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
	using request_type = request_head;
	using request_t = request_type;

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor,
		std::shared_ptr<detail::stream> stream, request_type request)
		requires core_concepts::match_sched<Exec0,executor_type>;

	~basic_server_exchange();

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] const request_type &request() const noexcept;
	[[nodiscard]] body_reader_type &request_body() noexcept;
	[[nodiscard]] body_writer_type &response_body() noexcept;

	template <typename Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Body, typename Token = use_sync_t>
	auto respond(response_head head, Body &&body, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code> &&
		requires(Body &&value) { std::data(value); std::size(value); };

	template <typename Token = use_sync_t>
	auto inform(response_head, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	executor_type executor_;
	std::shared_ptr<detail::server_exchange_state> state_;
	std::unique_ptr<body_reader_type> request_body_;
	std::unique_ptr<body_writer_type> response_body_;
};

using server_exchange = basic_server_exchange<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/server/detail/exchange.h>

#endif //RIWO_HTTP_EXP_SERVER_EXCHANGE_H
