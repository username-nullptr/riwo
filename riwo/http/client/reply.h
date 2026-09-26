// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_REPLY_H
#define RIWO_HTTP_CLIENT_REPLY_H

#include <riwo/http/protocol/utils/client/parser.h>
#include <riwo/http/protocol/utils/client/cookie_jar.h>
#include <riwo/http/client/connection_lease.h>

namespace riwo::http { namespace detail {
struct reply_access;
} //namespace detail

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_reply final :
	public const_headers<basic_reply<Exec>>,
	public const_cookies<cookie,basic_reply<Exec>>
{
	RIWO_DISABLE_COPY_MOVE(basic_reply)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using parser_t = client_parser;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using lease_t = basic_connection_lease<executor_t>;
	using lease_ptr = lease_t::ptr_t;

	explicit basic_reply(lease_ptr lease);
	basic_reply(lease_ptr lease, parser_t &&parser);
	~basic_reply() override;

public:
	// Synchronous I/O returns the value directly. The default token throws
	// std::system_error on failure; error_code& returns a default value and
	// stores the error. Async completion uses (error_code, value).
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,Value...>;

	template <typename T, typename Token>
	static constexpr bool file_task_token =
		task_token_v<Token,size_t> and
		concepts::file_opt_token_p <
			T, char, file_optype::single, io_permission::write
		>;

public:
	template <typename Token = use_sync_t>
	auto wait(Token &&token = {})
		requires task_token_v<Token,status_enum>;

	template <typename Token = use_sync_t>
	auto read(const mutable_buffer &buf, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <core_concepts::buffer Buffer, typename Token = use_sync_t>
	auto read(Token &&token = {})
		requires task_token_v<Token,Buffer>;

	template <typename Token = use_sync_t>
	auto read(Token &&token = {})
		requires task_token_v<Token,std::vector<std::byte>>;

	template <typename T, typename Token = use_sync_t>
	auto save_file(T &&opt, Token &&token = {})
		requires file_task_token<T,Token>;

	template <typename T, typename Progress, typename Token = use_sync_t>
	auto save_file(T &&opt, Progress &&progress, Token &&token = {})
		requires file_task_token<T,Token> and concepts::progress_callback<Progress,Token>;

public:
	[[nodiscard]] version_enum version() const noexcept;
	[[nodiscard]] status_enum status() const noexcept;

	[[nodiscard]] bool valid() const noexcept;
	[[nodiscard]] error_code first_error() const noexcept;

	[[nodiscard]] bool content_decoded() const noexcept;
	[[nodiscard]] bool is_chunked() const noexcept;
	[[nodiscard]] bool is_eof() const noexcept;

	[[nodiscard]] bool is_upgrade() const noexcept;
	[[nodiscard]] std::string take_pending_data();

	[[nodiscard]] const lease_t &lease() const noexcept;
	[[nodiscard]] lease_t &lease() noexcept;

	[[nodiscard]] const parser_t &parser() const noexcept;
	[[nodiscard]] parser_t &parser() noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;
	basic_reply &bind_cookie_jar(std::shared_ptr<cookie_jar> jar, url origin);
	basic_reply &cancel() noexcept;

private:
	friend struct detail::reply_access;
	class impl;
	std::shared_ptr<impl> m_impl;
};

} //namespace riwo::http
#include <riwo/http/client/detail/reply.h>


#endif //RIWO_HTTP_CLIENT_REPLY_H
