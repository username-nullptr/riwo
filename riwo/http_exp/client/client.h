// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_CLIENT_H
#define RIWO_HTTP_EXP_CLIENT_CLIENT_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/client/exchange.h>
#include <riwo/http_exp/client/request.h>
#include <riwo/http_exp/client/reply.h>

namespace riwo::http_exp::detail
{
class client_session;
class client_state;
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
	using config_type = client_config;
	using config_t = config_type;
	using exchange_type = basic_client_exchange<executor_type>;
	using exchange_t = exchange_type;
	using reply_type = basic_reply<executor_type>;
	using reply_t = reply_type;

	basic_client(basic_client &&) noexcept = default;
	basic_client &operator=(basic_client &&) noexcept = default;

	template <typename Exec0>
	explicit basic_client(Exec0 &&executor, config_type config = {})
		requires (
			!std::same_as<std::remove_cvref_t<Exec0>,basic_client> &&
			core_concepts::match_sched<Exec0,executor_type>);

	template <typename Exec0>
	basic_client(Exec0 &&executor,
		std::shared_ptr<detail::client_session> session,
		config_type config = {})
		requires (
			!std::same_as<std::remove_cvref_t<Exec0>,basic_client> &&
			core_concepts::match_sched<Exec0,executor_type>);

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] const config_type &config() const noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto send(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_type>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto fetch(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_type>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(request, body_spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,exchange_type>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto open(http_exp::method request_method, Url &&url,
		body_spec spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,exchange_type>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto get(Url &&url, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_type>;

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto post(Url &&url, const const_buffer &body,
		Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,reply_type>;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using client = basic_client<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/client.h>

#endif //RIWO_HTTP_EXP_CLIENT_CLIENT_H
