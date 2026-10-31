// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_EXCHANGE_H
#define RIWO_EXP_HTTP_SERVER_EXCHANGE_H

#include <riwo/experimental/http/protocol/duplex_channel.h>
#include <riwo/experimental/http/protocol/response_head.h>
#include <riwo/experimental/http/protocol/capabilities.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/protocol/parameters.h>
#include <riwo/experimental/http/protocol/body_spec.h>

#include <riwo/experimental/http/body/writer.h>
#include <riwo/experimental/http/body/reader.h>
#include <riwo/experimental/http/body/source.h>
#include <riwo/experimental/http/body/pipe.h>

#include <riwo/experimental/http/server/file.h>
#include <riwo/experimental/http/version.h>

namespace riwo::experimental::http { namespace detail {
class server_exchange_state;
} //namespace detail

class session_stream;
class app_session;
class app_session_manager;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_HTTP_TAPI basic_server_exchange
{
	RIWO_DISABLE_COPY_MOVE(basic_server_exchange)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using endpoint_t = asio::ip::tcp::endpoint;

	using body_reader_t = basic_body_reader<executor_t>;
	using body_writer_t = basic_body_writer<executor_t>;

	using session_stream_ptr = std::shared_ptr<session_stream>;
	using request_t = request_head;

	using app_session_ptr = std::shared_ptr<app_session>;
	using app_session_manager_ptr = std::shared_ptr<app_session_manager>;

public:
	basic_server_exchange()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	basic_server_exchange(session_stream_ptr stream, request_t request)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor, session_stream_ptr stream, request_t request)
		requires core_concepts::match_sched<Exec0,executor_t>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor, session_stream_ptr stream, request_t request,
		parameter_view path_parameters, parameter_view query_parameters,
		app_session_manager_ptr sessions, std::filesystem::path resource_root = {},
		bool automatic_compression = false, uint64_t compression_minimum_size = 1024
	) requires
		core_concepts::match_sched<Exec0,executor_t>;

	~basic_server_exchange();

public:
	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(body_spec, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {});

	// Asynchronous responses borrow body until completion, except detached,
	// which owns a copy.
	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(const const_buffer &body, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(response_head head, const const_buffer &body, Token &&token = {});

	template <concepts::body_source_model_p Source,
		core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(Source &&source, Token &&token = {});

	template <concepts::body_source_model_p Source,
		core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond(response_head head, Source &&source, Token &&token = {}) ;

public:
	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond_file(std::filesystem::path path, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto respond_file(std::filesystem::path path, file_response_options options, Token &&token = {});

	template <core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token = use_sync_t>
	[[nodiscard]] auto save_file(const std::filesystem::path &path, Token &&token = {});

	template <typename File,
		core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token = use_sync_t>
	[[nodiscard]] auto save_file(File &&file, Token &&token = {}) requires
		(not std::same_as<std::remove_cvref_t<File>,std::filesystem::path>) and
		std::constructible_from<file_sink,File>;

public:
	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto inform(response_head, Token &&token = {});

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept_duplex(response_head, Token &&token = {}) requires
		core_concepts::dis_detached_tf_opt_token<Token,error_code,basic_duplex_channel<executor_t>>;

public:
	[[nodiscard]] bool response_started() const noexcept;
	[[nodiscard]] const request_t &request() const noexcept;

	[[nodiscard]] body_reader_t &request_body() noexcept;
	[[nodiscard]] body_writer_t &response_body() noexcept;

	[[nodiscard]] const parameter_view &path_parameters() const noexcept;
	[[nodiscard]] const parameter_view &query_parameters() const noexcept;

	[[nodiscard]] optional<value> path_parameter(core_concepts::text_p<char> auto &&name) const;
	[[nodiscard]] optional<value> query_parameter(core_concepts::text_p<char> auto &&name) const;

	[[nodiscard]] app_session_ptr find_session();
	[[nodiscard]] app_session_ptr session();

public:
	void cancel() noexcept;
	[[nodiscard]] executor_t get_executor() const noexcept;

	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;

	[[nodiscard]] sys_expected<endpoint_t> remote_endpoint() const noexcept;
	[[nodiscard]] sys_expected<endpoint_t> local_endpoint() const noexcept;

	[[nodiscard]] const std::filesystem::path &resource_root() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using server_exchange = basic_server_exchange<>;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/server/detail/exchange.h>


#endif //RIWO_EXP_HTTP_SERVER_EXCHANGE_H
