// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_REPLY_H
#define RIWO_HTTP_EXP_CLIENT_REPLY_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/protocol/response_head.h>
#include <riwo/http_exp/protocol/version.h>

namespace riwo::http_exp::detail
{
class body_reader_state;
}

namespace riwo::http_exp
{

using reply_head = response_head;

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_reply
{
	RIWO_DISABLE_COPY(basic_reply)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_t = basic_body_reader<executor_t>;

	basic_reply()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_reply(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	basic_reply(protocol selected_protocol, reply_head head,
		std::shared_ptr<detail::body_reader_state> body,
		std::vector<std::byte> buffered_body = {},
		std::shared_ptr<void> owner = {},
		uint64_t max_buffered_body = 8U * 1024U * 1024U,
		protocol reply_protocol = protocol::v1_1)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_reply(Exec0 &&executor, protocol selected_protocol,
		reply_head head, std::shared_ptr<detail::body_reader_state> body,
		std::vector<std::byte> buffered_body = {},
		std::shared_ptr<void> owner = {},
		uint64_t max_buffered_body = 8U * 1024U * 1024U,
		protocol reply_protocol = protocol::v1_1)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_reply();
	basic_reply(basic_reply &&) noexcept = default;
	basic_reply &operator=(basic_reply &&) noexcept = default;

	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] protocol http_version() const noexcept;
	[[nodiscard]] http_exp::status status() const noexcept;
	[[nodiscard]] const fields &headers() const noexcept;
	[[nodiscard]] body_reader_t &body() noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto read_all(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,std::vector<std::byte>>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto save_file(std::filesystem::path path, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>;

	[[nodiscard]] std::span<const std::byte> buffered_body() const noexcept;
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using reply_t = basic_reply<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/reply.h>

#endif //RIWO_HTTP_EXP_CLIENT_REPLY_H
