// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_CLIENT_H
#define RIWO_HTTP_CLIENT_CLIENT_H

#include <riwo/http/client/connection_pool.h>
#include <riwo/http/client/request_context.h>
#include <riwo/http/client/proxy.h>

namespace riwo::http { namespace detail {
struct client_access;
} //namespace detail

struct client_config
{
	proxy_t default_proxy = use_global_proxy;
	bool no_delay = true;
};
/**
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe. Use one strand for calls and completions that
 * access the same client when its execution context has multiple runners.
 */
template <core_concepts::exec Exec = asio::any_io_executor,
		  version_enum Version = version::v11>
class RIWO_HTTP_TAPI basic_client
{
	RIWO_DISABLE_COPY(basic_client)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	static constexpr auto version_v = Version;

	using config_t = client_config;
	using connection_pool_t = basic_connection_pool<executor_t>;

	using connection_t = connection_pool_t::connection_t;
	using connection_ptr = connection_pool_t::connection_ptr;

	template <method_enum Method>
	using context_t = basic_request_context<Method, executor_t, version_v>;

	template <method_enum Method>
	using context_ptr = std::shared_ptr<context_t<Method>>;

	using reply_t = basic_reply<executor_t>;
	using request_arg_t = request_arg;
	using url_t = riwo::url;

public:
	struct req_info
	{
		url_t url {};
		request_arg_t arg {};
		std::optional<proxy_t> proxy {};

		size_t max_redirects = 0;
		bool auto_decompression = true;

		req_info(core_concepts::string_p<char> auto &&request_url) :
			url(std::forward<decltype(request_url)>(request_url)) {}

		req_info(url_t request_url, request_arg_t request_options) :
			url(std::move(request_url)), arg(std::move(request_options)) {}

		req_info(url_t request_url) :
			url(std::move(request_url)) {}
	};

	template <method_enum Method, typename Token>
	static constexpr bool request_token_v =
		core_concepts::dis_detached_tf_opt_token <
			Token, error_code, context_ptr<Method>
		>;

	template <typename T, typename Token>
	static constexpr bool upload_file_opt_token_v =
		concepts::file_opt_token_p <
			T, char, file_optype::combine, io_permission::read
		> and
		core_concepts::dis_detached_tf_opt_token <
			Token, error_code, context_ptr<method::put>
		>;

	template <typename T, typename Token>
	static constexpr bool download_file_opt_token_v =
		concepts::file_opt_token_p <
			T, char, file_optype::single, io_permission::write
		> and
		core_concepts::dis_detached_tf_opt_token <
			Token, error_code, context_ptr<method::get>
		>;

public:
	explicit basic_client(config_t config = {}) requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_client(Exec0 &&exec, config_t config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_t>
	);
	explicit basic_client(connection_pool_t &&pool, config_t config = {});

	basic_client(basic_client &&other) noexcept;
	basic_client &operator=(basic_client &&other) noexcept;
	~basic_client();

public:
	template <method_enum Method, typename Token = use_sync_t>
	[[nodiscard]] auto request(req_info info, Token &&token = {})
		requires request_token_v<Method,Token>;

	template <typename T, typename Token = use_sync_t>
	auto upload_file(req_info info, T &&opt, Token &&token = {})
		requires upload_file_opt_token_v<T,Token>;

	template <typename T, typename Progress, typename Token = use_sync_t>
	auto upload_file(req_info info, T &&opt, Progress &&progress, Token &&token = {})
		requires upload_file_opt_token_v<T,Token> and concepts::progress_handler<Progress,Token>;

	template <typename T, typename Token = use_sync_t>
	auto download_file(req_info info, T &&opt, Token &&token = {})
		requires download_file_opt_token_v<T,Token>;

	template <typename T, typename Progress, typename Token = use_sync_t>
	auto download_file(req_info info, T &&opt, Progress &&progress, Token &&token = {})
		requires download_file_opt_token_v<T,Token> and concepts::progress_handler<Progress,Token>;

public:
	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_get(req_info info, Token &&token = {})
		requires request_token_v<method::get,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_put(req_info info, Token &&token = {})
		requires request_token_v<method::put,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_post(req_info info, Token &&token = {})
		requires request_token_v<method::post,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_head(req_info info, Token &&token = {})
		requires request_token_v<method::head,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_patch(req_info info, Token &&token = {})
		requires request_token_v<method::patch,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_delete(req_info info, Token &&token = {})
		requires request_token_v<method::delet,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_options(req_info info, Token &&token = {})
		requires request_token_v<method::options,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_trace(req_info info, Token &&token = {})
		requires request_token_v<method::trace,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto request_connect(req_info info, Token &&token = {})
		requires request_token_v<method::connect,Token>;

public:
	template <method_enum Method, typename Token = use_sync_t>
	[[nodiscard]] auto make_context(req_info info, Token &&token = {})
		requires request_token_v<Method,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_get(req_info info, Token &&token = {})
		requires request_token_v<method::get,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_put(req_info info, Token &&token = {})
		requires request_token_v<method::put,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_post(req_info info, Token &&token = {})
		requires request_token_v<method::post,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_head(req_info info, Token &&token = {})
		requires request_token_v<method::head,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_patch(req_info info, Token &&token = {})
		requires request_token_v<method::patch,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_delete(req_info info, Token &&token = {})
		requires request_token_v<method::delet,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_options(req_info info, Token &&token = {})
		requires request_token_v<method::options,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_trace(req_info info, Token &&token = {})
		requires request_token_v<method::trace,Token>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto make_connect(req_info info, Token &&token = {})
		requires request_token_v<method::connect,Token>;

public:
	[[nodiscard]] std::shared_ptr<cookie_jar> cookie_store() noexcept;
	[[nodiscard]] config_t config() const;

	[[nodiscard]] static consteval version_enum version() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

private:
	friend struct detail::client_access;
	class impl;
	std::shared_ptr<impl> m_impl;
};

using client = basic_client<>;

} //namespace riwo::http
#include <riwo/http/client/detail/client.h>


#endif //RIWO_HTTP_CLIENT_CLIENT_H
