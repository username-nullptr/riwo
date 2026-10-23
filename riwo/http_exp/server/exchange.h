// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_EXCHANGE_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/body/pipe.h>
#include <riwo/http_exp/body/source.h>
#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/body/writer.h>
#include <riwo/http_exp/protocol/capabilities.h>
#include <riwo/http_exp/protocol/duplex_channel.h>
#include <riwo/http_exp/protocol/parameters.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol/version.h>
#include <riwo/http_exp/server/file.h>
#include <typeinfo>

namespace riwo::http_exp::detail
{
class server_exchange_state;
class stream;
}

namespace riwo::http_exp
{

class application_session;
class application_session_manager;
using application_session_ptr = std::shared_ptr<application_session>;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_server_exchange
{
	RIWO_DISABLE_COPY_MOVE(basic_server_exchange)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_t = basic_body_reader<executor_t>;
	using body_writer_t = basic_body_writer<executor_t>;
	using request_t = request_head;

	basic_server_exchange()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_server_exchange(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	basic_server_exchange(std::shared_ptr<detail::stream> stream,
		request_t request)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor,
		std::shared_ptr<detail::stream> stream, request_t request)
		requires core_concepts::match_sched<Exec0,executor_t>;

	template <typename Exec0>
	basic_server_exchange(Exec0 &&executor,
		std::shared_ptr<detail::stream> stream, request_t request,
		parameter_view path_parameters, parameter_view query_parameters,
		std::shared_ptr<application_session_manager> sessions,
		std::filesystem::path resource_root = {},
		bool automatic_compression = false,
		uint64_t compression_minimum_size = 1024)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_server_exchange();

	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	remote_endpoint(error_code &error) const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	local_endpoint(error_code &error) const noexcept;
	[[nodiscard]] const request_t &request() const noexcept;
	[[nodiscard]] const parameter_view &path_parameters() const noexcept;
	[[nodiscard]] const parameter_view &query_parameters() const noexcept;
	[[nodiscard]] optional<std::string_view>
	path_parameter(std::string_view name) const noexcept;
	[[nodiscard]] optional<std::string_view>
	query_parameter(std::string_view name) const noexcept;
	[[nodiscard]] bool response_started() const noexcept;
	[[nodiscard]] application_session_ptr find_session();
	[[nodiscard]] application_session_ptr session();
	[[nodiscard]] const std::filesystem::path &resource_root() const noexcept;
	[[nodiscard]] body_reader_t &request_body() noexcept;
	[[nodiscard]] body_writer_t &response_body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto save_file(std::filesystem::path path,
		Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,uint64_t>;

	template <typename File, typename Token = use_sync_t>
	[[nodiscard]] auto save_file(File &&file, Token &&token = {})
		requires (not std::same_as<std::remove_cvref_t<File>,
			std::filesystem::path>)
			and std::constructible_from<file_sink,File>
			and core_concepts::dis_detached_tf_opt_token<
				Token,error_code,uint64_t>;

	template <typename Token = use_sync_t>
	auto respond(body_spec, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto respond(response_head, body_spec, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	// Asynchronous responses borrow body until completion, except detached,
	// which owns a copy.
	template <typename Token = use_sync_t>
	auto respond(const const_buffer &body, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto respond(response_head head, const const_buffer &body, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Source, typename Token = use_sync_t>
	auto respond(Source &&source, Token &&token = {})
		requires concepts::body_source_model<std::remove_cvref_t<Source>>
			and core_concepts::tf_opt_token<Token,error_code>;

	template <typename Source, typename Token = use_sync_t>
	auto respond(response_head head, Source &&source, Token &&token = {})
		requires concepts::body_source_model<std::remove_cvref_t<Source>>
			and core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto respond_file(std::filesystem::path path,
		Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto respond_file(std::filesystem::path path,
		file_response_options options, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	auto inform(response_head, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto accept_duplex(response_head, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,basic_duplex_channel<executor_t>>;

	void cancel() noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;

	template <typename Extension>
	[[nodiscard]] Extension *extension() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using server_exchange = basic_server_exchange<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/server/detail/exchange.h>

#endif //RIWO_HTTP_EXP_SERVER_EXCHANGE_H
