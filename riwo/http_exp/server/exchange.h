// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_EXCHANGE_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/body/writer.h>
#include <riwo/http_exp/protocol/capabilities.h>
#include <riwo/http_exp/protocol/duplex_channel.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol/version.h>

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
	using body_reader_t = basic_body_reader<executor_t>;
	using body_writer_t = basic_body_writer<executor_t>;
	using request_t = request_head;

	basic_server_exchange()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	basic_server_exchange(std::shared_ptr<detail::stream> stream,
		request_t request)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor,
		std::shared_ptr<detail::stream> stream, request_t request)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_server_exchange();

	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	remote_endpoint(error_code &error) const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	local_endpoint(error_code &error) const noexcept;
	[[nodiscard]] const request_t &request() const noexcept;
	[[nodiscard]] body_reader_t &request_body() noexcept;
	[[nodiscard]] body_writer_t &response_body() noexcept;

	template <typename Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	// Asynchronous responses borrow body until completion, except detached,
	// which owns a copy.
	template <typename Token = use_sync_t>
	auto respond(response_head head, const const_buffer &body, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto inform(response_head, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept_duplex(response_head, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,basic_duplex_channel<executor_t>>;

	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using server_exchange_t = basic_server_exchange<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/server/detail/exchange.h>

#endif //RIWO_HTTP_EXP_SERVER_EXCHANGE_H
