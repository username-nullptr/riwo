// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_CLIENT_H
#define RIWO_HTTP_EXP_CLIENT_CLIENT_H

#include <riwo/core/execution.h>
#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/client/exchange.h>
#include <riwo/http_exp/client/request.h>
#include <riwo/http_exp/client/reply.h>
#include <riwo/http_exp/transport/connector.h>

namespace riwo::http_exp::detail
{
class client_session;
class client_state;
class connector;
}

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_client
{
	RIWO_DISABLE_COPY(basic_client)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using config_t = client_config;
	using exchange_t = basic_client_exchange<executor_t>;
	using reply_t = basic_reply<executor_t>;

	basic_client(basic_client &&other) noexcept;
	basic_client &operator=(basic_client &&other) noexcept;

	explicit basic_client(config_t config = {})
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	explicit basic_client(
		std::shared_ptr<detail::client_session> session,
		config_t config = {})
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	explicit basic_client(
		std::shared_ptr<basic_transport_connector<executor_t>> connector,
		config_t config = {})
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_client(Exec0 &&executor, config_t config = {})
		requires (
			not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
			core_concepts::match_sched<Exec0,executor_t>);

	template <typename Exec0>
	basic_client(Exec0 &&executor,
		std::shared_ptr<detail::client_session> session,
		config_t config = {})
		requires (
			not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
			core_concepts::match_sched<Exec0,executor_t>);

	template <typename Exec0>
	basic_client(Exec0 &&executor,
		std::shared_ptr<basic_transport_connector<executor_t>> connector,
		config_t config = {})
		requires (
			not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
			core_concepts::match_sched<Exec0,executor_t>);

	[[nodiscard]] executor_t get_executor() const noexcept;
	basic_client &set_config(config_t config);
	[[nodiscard]] config_t config() const;
	[[nodiscard]] std::shared_ptr<cookie_jar> cookie_store() const noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto send(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_t>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto fetch(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_t>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(request, body_spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,exchange_t>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto open(http_exp::method request_method, Url &&url,
		body_spec spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,exchange_t>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto get(Url &&url, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_t>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto post(Url &&url, const const_buffer &body,
		Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_t>;

private:
	void reset_state(config_t config);
	executor_t m_exec;
	config_t m_config;
	std::shared_ptr<detail::client_session> m_session;
	std::shared_ptr<detail::connector> m_connector;
	std::shared_ptr<detail::client_state> m_state;
};

using client = basic_client<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/client.h>

#endif //RIWO_HTTP_EXP_CLIENT_CLIENT_H
