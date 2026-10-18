// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
#define RIWO_HTTP_EXP_CLIENT_EXCHANGE_H

#include <riwo/http_exp/body.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp::detail
{
class client_exchange_state;
class stream;
}

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
		requires std::default_initializable<executor_type>;

	template <typename Exec0>
	explicit basic_client_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type>;

	template <typename Exec0>
	basic_client_exchange(Exec0 &&executor,
		std::shared_ptr<detail::stream> stream)
		requires core_concepts::match_sched<Exec0,executor_type>;

	~basic_client_exchange();
	basic_client_exchange(basic_client_exchange &&) noexcept = default;
	basic_client_exchange &operator=(basic_client_exchange &&) noexcept = default;

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] body_writer_type &request_body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto wait_response(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response_head>;

	[[nodiscard]] body_reader_type &response_body() noexcept;
	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	executor_type executor_;
	std::shared_ptr<detail::client_exchange_state> state_;
	std::unique_ptr<body_writer_type> request_body_;
	std::unique_ptr<body_reader_type> response_body_;
};

using client_exchange = basic_client_exchange<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/exchange.h>

#endif //RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
