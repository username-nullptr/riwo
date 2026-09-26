// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_REQUEST_CONTEXT_H
#define RIWO_HTTP_CLIENT_REQUEST_CONTEXT_H

#include <riwo/http/protocol/utils/client/generator.h>
#include <riwo/http/protocol/utils/client/request_arg.h>
#include <riwo/http/client/reply.h>
#include <riwo/core/url.h>

namespace riwo::http
{

template <method_enum Method,
		  core_concepts::exec Exec = asio::any_io_executor,
		  version_enum Version = version::v11>
class RIWO_HTTP_TAPI basic_request_context final :
	public mutable_headers<basic_request_context<Method,Exec,Version>>,
	public mutable_cookies<value,basic_request_context<Method,Exec,Version>>,
	public mutable_chunk_attributes<basic_request_context<Method,Exec,Version>>
{
	RIWO_DISABLE_COPY_MOVE(basic_request_context)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using lease_t = basic_connection_lease<executor_t>;
	using lease_ptr = lease_t::ptr_t;

	using url_t = riwo::url;
	using method_t = http::method;
	using request_arg_t = request_arg;

	using reply_t = basic_reply<executor_t>;
	using reply_ptr = std::shared_ptr<reply_t>;
	using const_reply_ptr = std::shared_ptr<const reply_t>;

	using value_t = request_arg_t::value_t;
	using generator_t = client_generator;
	using headers_t = request_arg_t::headers_t;

	static constexpr auto method_v = Method;
	static constexpr auto version_v = Version;

	static constexpr auto put_or_post =
		method_v == method_t::post or method_v == method_t::put;

public:
	struct options
	{
		request_arg_t arg {};
		std::shared_ptr<cookie_jar> cookie_store {};
		request_target_form target_form = request_target_form::origin;
		bool auto_decompression = true;
	};
	basic_request_context(lease_ptr &&lease, url_t url, options opt = {});
	~basic_request_context() override;

public:
	// Byte counts describe caller-supplied body bytes only; request headers and
	// transfer framing are excluded. error_code& and asynchronous completions
	// preserve the partial body count when an error is also reported. The
	// throwing/expected synchronous form can only report the error on failure.
	// Asynchronous writes borrow body until completion, except detached, which
	// owns a copy. chunk_end has no caller body and therefore returns zero.
	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(Token &&token = {});

	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(const const_buffer &body, Token &&token = {})
		requires put_or_post;

	template <typename T, typename Token>
	static constexpr bool file_task_token_v =
		method_v == method_t::put and
		core_concepts::tf_opt_token<Token,error_code,size_t> and
		concepts::file_opt_token_p <
			T, char, file_optype::combine, io_permission::read
		>;

	template <typename T, typename Token = use_sync_t>
	auto upload_file(body_norms_t norms, T &&opt, Token &&token = {})
		requires file_task_token_v<T,Token>;

	template <typename T, typename Progress, typename Token = use_sync_t>
	auto upload_file(body_norms_t norms, T &&opt, Progress &&progress, Token &&token = {})
		requires file_task_token_v<T,Token> and concepts::progress_callback<Progress,Token>;

public:
	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto chunk_end(const headers_t &completion_headers, Token &&token = {})
		requires put_or_post;

	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto chunk_end(Token &&token = {})
		requires put_or_post;

public:
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,Value...>;

	template <typename Token = use_sync_t>
	auto wait_reply(Token &&token = {})
		requires task_token_v<Token,status_enum>;

	[[nodiscard]] const_reply_ptr reply() const noexcept;
	[[nodiscard]] reply_ptr reply() noexcept;

	[[nodiscard]] bool responded() const noexcept;
	basic_request_context &cancel() noexcept;

public:
	[[nodiscard]] const url_t &url() const noexcept;
	[[nodiscard]] request_arg_t arg() const noexcept;
	[[nodiscard]] operator request_arg_t() const noexcept;

	[[nodiscard]] static consteval method_enum method() noexcept;
	[[nodiscard]] static consteval version_enum version() noexcept;

public:
	[[nodiscard]] const lease_t &lease() const noexcept;
	[[nodiscard]] lease_t &lease() noexcept;

	[[nodiscard]] const generator_t &generator() const noexcept;
	[[nodiscard]] generator_t &generator() noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <method_enum Method, version_enum Version = version::v11>
using request_context = basic_request_context<Method, asio::any_io_executor, Version>;

} //namespace riwo::http
#include <riwo/http/client/detail/request_context.h>


#endif //RIWO_HTTP_CLIENT_REQUEST_CONTEXT_H
