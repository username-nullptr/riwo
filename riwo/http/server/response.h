// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_RESPONSE_H
#define RIWO_HTTP_SERVER_RESPONSE_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/utils/connection.h>

namespace riwo::http
{

template <core_concepts::exec Exec>
class basic_request;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_response :
	public mutable_headers<basic_response<Exec>>,
	public mutable_cookies<cookie,basic_response<Exec>>,
	public mutable_chunk_attributes<basic_response<Exec>>
{
	RIWO_DISABLE_COPY_MOVE(basic_response)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using request_t = basic_request<executor_t>;

	using value_t = riwo::value;
	using headers_t = http::headers;

public:
	explicit basic_response(connection_ptr conn,
		std::filesystem::path resource_root = {}
	);
	~basic_response() override;

public:
	[[nodiscard]] version_enum version() const noexcept;
	basic_response &set_status(status_enum status);
	basic_response &auto_set(request_t &req);

	basic_response &set_auto_compression(bool enabled = true) noexcept;
	[[nodiscard]] bool auto_compression() const noexcept;

public:
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		core_concepts::dis_func_tf_opt_token<Token,error_code,Value...>;

	// Byte counts describe caller-supplied body bytes only; response headers and
	// transfer framing are excluded. send_file counts source-file bytes, while
	// redirect, continues, and chunk_end have no caller body and return zero.
	// error_code& and asynchronous completions preserve partial counts alongside
	// an error. Asynchronous writes borrow body until completion; detached owns a
	// copy.
	template <typename Token = use_sync_t>
	auto write(const const_buffer &body, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto write(Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename T, typename Token>
	static constexpr bool file_task_token_v =
		task_token_v<Token,size_t> and concepts::file_opt_token_p <
			T, char, file_optype::single, io_permission::read
		>;
	template <typename T, typename Token = use_sync_t>
	auto send_file(T &&opt, Token &&token = {})
		requires file_task_token_v<T,Token>;

public:
	template <typename Token = use_sync_t>
	auto redirect(core_concepts::text_p<char> auto &&url, redirect_enum redi, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto redirect(core_concepts::text_p<char> auto &&url, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto continues(Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto chunk_end(const headers_t &trailing_headers, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <typename Token = use_sync_t>
	auto chunk_end(Token &&token = {})
		requires task_token_v<Token,size_t>;

public:
	[[nodiscard]] status_enum status() const noexcept;
	[[nodiscard]] bool is_finished() const noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;
	basic_response &cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using response = basic_response<>;

} //namespace riwo::http
#include <riwo/http/server/detail/response.h>


#endif //RIWO_HTTP_SERVER_RESPONSE_H
