// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_REQUEST_H
#define RIWO_HTTP_SERVER_REQUEST_H

#include <riwo/http/protocol/utils/server/parser.h>
#include <riwo/http/protocol/utils/core/upgrade.h>
#include <riwo/http/utils/connection.h>

namespace riwo::http
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_request :
	public const_headers<basic_request<Exec>>,
	public const_cookies<value,basic_request<Exec>>,
	public const_parameters<basic_request<Exec>>
{
	RIWO_DISABLE_COPY_MOVE(basic_request)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using parser_t = server_parser;
	using value_t = parser_t::value_t;
	using path_args_t = parser_t::path_args_t;

	using parameters_t = http::parameters;
	using headers_t = http::headers;

public:
	explicit basic_request(connection_ptr connection,
		std::filesystem::path resource_root = {}
	);
	basic_request(connection_ptr connection, parser_t &&parser,
		std::filesystem::path resource_root = {}
	);
	~basic_request() override;

public:
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		core_concepts::dis_func_tf_opt_token<Token,error_code,Value...> and
		not is_detached_v<token_unbound_t<Token>>;

	template <typename Token = use_sync_t>
	auto wait(Token &&token = {}) requires task_token_v<Token>;

	int32_t path_match(std::string_view rule);

public:
	[[nodiscard]] method_enum method() const noexcept;
	[[nodiscard]] request_target_form target_form() const noexcept;
	[[nodiscard]] std::string_view target() const noexcept;
	[[nodiscard]] version_enum version() const noexcept;
	[[nodiscard]] std::string_view path() const noexcept;

public:
	[[nodiscard]] optional<value_t> path_arg (
		const core_concepts::text_p<char> auto &key
	) const noexcept;

	[[nodiscard]] bool contains_path_arg (
		const core_concepts::text_p<char> auto &key
	) const noexcept;

	[[nodiscard]] optional<value_t> path_arg(size_t index) const;
	[[nodiscard]] bool contains_path_arg(size_t index) const noexcept;

	[[nodiscard]] const parameters_t &path_args() const noexcept;

public:
	template <typename Token = use_sync_t>
	auto read(const mutable_buffer &buf, Token &&token = {})
		requires task_token_v<Token,size_t>;

	template <core_concepts::buffer Buffer, typename Token = use_sync_t>
	auto read(Token &&token = {})
		requires task_token_v<Token,Buffer>;

	template <typename Token = use_sync_t>
	auto read(Token &&token = {})
		requires task_token_v<Token,std::vector<std::byte>>;

	template <typename T, typename Token>
	static constexpr bool file_task_token_v =
		task_token_v<Token,size_t> and concepts::file_opt_token_p <
			T, char, file_optype::single, io_permission::write
		>;
	template <typename T, typename Token = use_sync_t>
	auto save_file(T &&opt, Token &&token = {})
		requires file_task_token_v<T,Token>;

public:
	[[nodiscard]] bool keep_alive() const noexcept;
	[[nodiscard]] bool support_gzip() const noexcept;
	[[nodiscard]] bool is_chunked() const noexcept;
	[[nodiscard]] bool can_read_body() const noexcept;
	[[nodiscard]] bool is_eof() const noexcept;
	[[nodiscard]] bool is_upgrade() const noexcept;
	[[nodiscard]] std::string take_pending_data();

public:
	[[nodiscard]] endpoint remote_endpoint() const;
	[[nodiscard]] endpoint local_endpoint() const;

	[[nodiscard]] executor_t get_executor() noexcept;
	basic_request &cancel() noexcept;

public:
	[[nodiscard]] const connection_t &connection() const noexcept;
	[[nodiscard]] connection_t &connection() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using request = basic_request<>;

} //namespace riwo::http
#include <riwo/http/server/detail/request.h>


#endif //RIWO_HTTP_SERVER_REQUEST_H
