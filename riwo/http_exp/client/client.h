// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_CLIENT_H
#define RIWO_HTTP_EXP_CLIENT_CLIENT_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/client/exchange.h>
#include <riwo/http_exp/client/request.h>
#include <riwo/http_exp/client/response.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_client
{
public:
	using executor_type = Exec;
	using config_type = client_config;

	basic_client(const basic_client &) = delete;
	basic_client &operator=(const basic_client &) = delete;
	basic_client(basic_client &&) noexcept = default;
	basic_client &operator=(basic_client &&) noexcept = default;

	template <typename Exec0>
	explicit basic_client(Exec0 &&executor, config_type config = {})
		requires (
			!std::same_as<std::remove_cvref_t<Exec0>,basic_client> &&
			core_concepts::match_sched<Exec0,executor_type>) :
		executor_(get_executor_helper(std::forward<Exec0>(executor))),
		config_(std::move(config))
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }
	[[nodiscard]] const config_type &config() const noexcept { return config_; }

	template <typename Token = use_sync_t>
	[[nodiscard]] auto send(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response>
	{
		return detail::unsupported_result<response>(
			executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	[[nodiscard]] auto fetch(request, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response>
	{
		return detail::unsupported_result<response>(
			executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	[[nodiscard]] auto open(request, body_spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,client_exchange>
	{
		return detail::unsupported_result<client_exchange>(
			executor_, std::forward<Token>(token));
	}

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto open(http_exp::method request_method, Url &&url,
		body_spec spec, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,client_exchange>
	{
		return open(request(std::move(request_method),
			riwo::url(std::forward<Url>(url))), std::move(spec),
			std::forward<Token>(token));
	}

	template <typename Url, typename Token = use_sync_t>
	[[nodiscard]] auto get(Url &&url, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response>
	{
		return send(request(method::get, riwo::url(std::forward<Url>(url))),
			std::forward<Token>(token));
	}

	template <typename Url, typename Body, typename Token = use_sync_t>
	[[nodiscard]] auto post(Url &&url, Body &&body, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,response>
	{
		request value(method::post, riwo::url(std::forward<Url>(url)));
		value.body(std::forward<Body>(body));
		return send(std::move(value), std::forward<Token>(token));
	}

private:
	executor_type executor_;
	config_type config_;
};

using client = basic_client<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_CLIENT_H
