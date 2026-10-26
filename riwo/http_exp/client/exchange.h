// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
#define RIWO_HTTP_EXP_CLIENT_EXCHANGE_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/protocol/body_spec.h>
#include <riwo/http_exp/body/writer.h>
#include <riwo/http_exp/client/cookie_jar.h>
#include <riwo/http_exp/client/reply.h>
#include <riwo/http_exp/protocol/capabilities.h>
#include <riwo/http_exp/protocol/duplex_channel.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/version.h>
#include <typeinfo>

namespace riwo::http_exp::detail
{
class client_exchange_state;
}

namespace riwo::http_exp
{
class session_stream;
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
	using body_reader_t = basic_body_reader<executor_t>;
	using body_writer_t = basic_body_writer<executor_t>;

	basic_client_exchange()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_client_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_client_exchange(std::shared_ptr<session_stream> stream,
		std::shared_ptr<cookie_jar> cookies = {}, riwo::url origin = {})
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_client_exchange(Exec0 &&executor,
		std::shared_ptr<session_stream> stream,
		std::shared_ptr<cookie_jar> cookies = {}, riwo::url origin = {})
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_client_exchange();
	basic_client_exchange(basic_client_exchange &&other) noexcept;
	basic_client_exchange &operator=(basic_client_exchange &&other) noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] body_writer_t &request_body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto wait_reply(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,reply_head>;

	[[nodiscard]] body_reader_t &reply_body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto release_duplex(Token &&token = {})
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

using client_exchange = basic_client_exchange<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/exchange.h>

#endif //RIWO_HTTP_EXP_CLIENT_EXCHANGE_H
