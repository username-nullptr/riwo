// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_REPLY_H
#define RIWO_EXP_HTTP_CLIENT_REPLY_H

#include <riwo/experimental/http/protocol/response_head.h>
#include <riwo/experimental/http/body/pipe.h>
#include <riwo/experimental/http/version.h>

namespace riwo::experimental::http { namespace detail {
class body_reader_state;
} //namespace detail

using reply_head = response_head;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_HTTP_TAPI basic_reply
{
	RIWO_DISABLE_COPY(basic_reply)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using status_t = experimental::http::status;
	using body_reader_t = basic_body_reader<executor_t>;

public:
	template <typename Exec0>
	explicit basic_reply(Exec0 &&executor) requires
		core_concepts::match_sched<Exec0,executor_t>;

	basic_reply(protocol selected_protocol, reply_head head, detail::body_reader_state_ptr body,
		std::vector<std::byte> buffered_body = {}, std::shared_ptr<void> owner = {},
		uint64_t max_buffered_body = 8U * 1024U * 1024U, protocol reply_protocol = protocol::v1_1
	) requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_reply(Exec0 &&executor, protocol selected_protocol, reply_head head,
		detail::body_reader_state_ptr body, std::vector<std::byte> buffered_body = {},
		std::shared_ptr<void> owner = {}, uint64_t max_buffered_body = 8U * 1024U * 1024U,
		protocol reply_protocol = protocol::v1_1
	) requires
		core_concepts::match_sched<Exec0,executor_t>;

	basic_reply() requires
		core_concepts::match_sched<io_executor_t,executor_t>;
	~basic_reply();

	basic_reply(basic_reply &&other) noexcept;
	basic_reply &operator=(basic_reply &&other) noexcept;

public:

	template <core_concepts::dis_detached_tf_opt_token
		<error_code,std::vector<std::byte>> Token = use_sync_t>
	[[nodiscard]] auto read_all(Token &&token = {});

	template <core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token = use_sync_t>
	[[nodiscard]] auto save_file(const std::filesystem::path &path, Token &&token = {});

	template <typename File,
		core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token = use_sync_t>
	[[nodiscard]] auto save_file(File &&file, Token &&token = {}) requires
		(not std::same_as<std::remove_cvref_t<File>,std::filesystem::path>) and
		std::constructible_from<file_sink,File>;

	void cancel() noexcept;

public:
	[[nodiscard]] status_t status() const noexcept;
	[[nodiscard]] const fields &headers() const noexcept;

	[[nodiscard]] body_reader_t &body() noexcept;
	[[nodiscard]] std::span<const std::byte> buffered_body() const noexcept;

	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] protocol http_version() const noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using reply = basic_reply<>;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/client/detail/reply.h>


#endif //RIWO_EXP_HTTP_CLIENT_REPLY_H
