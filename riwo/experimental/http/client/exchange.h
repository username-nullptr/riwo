// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_EXCHANGE_H
#define RIWO_EXP_HTTP_CLIENT_EXCHANGE_H

#include <riwo/experimental/http/protocol/duplex_channel.h>
#include <riwo/experimental/http/protocol/response_head.h>

#include <riwo/experimental/http/protocol/capabilities.h>
#include <riwo/experimental/http/protocol/body_spec.h>

#include <riwo/experimental/http/client/cookie_jar.h>
#include <riwo/experimental/http/client/reply.h>

#include <riwo/experimental/http/body/reader.h>
#include <riwo/experimental/http/body/writer.h>
#include <riwo/experimental/http/version.h>

namespace riwo::experimental::http { namespace detail {
class client_exchange_state;
} //namespace detail

class session_stream;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_HTTP_TAPI basic_client_exchange
{
	RIWO_DISABLE_COPY(basic_client_exchange)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using body_reader_t = basic_body_reader<executor_t>;
	using body_writer_t = basic_body_writer<executor_t>;

	using session_stream_ptr = std::shared_ptr<session_stream>;

public:
	template <typename Exec0>
	explicit basic_client_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_client_exchange
	(session_stream_ptr stream, cookie_jar_ptr cookies = {}, url origin = {})
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_client_exchange(Exec0 &&executor,
		session_stream_ptr stream, cookie_jar_ptr cookies = {}, url origin = {}
	) requires
		core_concepts::match_sched<Exec0,executor_t>;

	basic_client_exchange() requires
		core_concepts::match_sched<io_executor_t,executor_t>;
	~basic_client_exchange();

	basic_client_exchange(basic_client_exchange &&other) noexcept;
	basic_client_exchange &operator=(basic_client_exchange &&other) noexcept;

public:
	template <core_concepts::dis_detached_tf_opt_token<error_code,reply_head> Token = use_sync_t>
	[[nodiscard]] auto wait_reply(Token &&token = {});

	template <typename Token = use_sync_t>
	[[nodiscard]] auto release_duplex(Token &&token = {}) requires
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_duplex_channel<executor_t>>;

	void cancel() noexcept;

public:
	[[nodiscard]] body_writer_t &request_body() noexcept;
	[[nodiscard]] body_reader_t &reply_body() noexcept;

	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using client_exchange = basic_client_exchange<>;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/client/detail/exchange.h>


#endif //RIWO_EXP_HTTP_CLIENT_EXCHANGE_H
