// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_RESPONSE_H
#define RIWO_HTTP_EXP_CLIENT_RESPONSE_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_response
{
	RIWO_DISABLE_COPY(basic_response)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using body_reader_type = basic_body_reader<executor_type>;
	using body_reader_t = body_reader_type;

	basic_response()
		requires std::default_initializable<executor_type> :
		basic_response(executor_type{})
	{
	}

	template <typename Exec0>
	explicit basic_response(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type> :
		executor_(get_executor_helper(std::forward<Exec0>(executor))),
		body_(new body_reader_type(executor_))
	{
	}

	~basic_response() = default;
	basic_response(basic_response &&) noexcept = default;
	basic_response &operator=(basic_response &&) noexcept = default;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }
	[[nodiscard]] protocol selected_protocol() const noexcept { return protocol_; }
	[[nodiscard]] http_exp::status status() const noexcept { return head_.status; }
	[[nodiscard]] const fields &headers() const noexcept { return head_.headers; }
	[[nodiscard]] body_reader_type &body() noexcept { return *body_; }

	template <typename Token = use_sync_t>
	[[nodiscard]] auto read_all(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,std::vector<std::byte>>
	{
		return detail::unsupported_result<std::vector<std::byte>>(
			executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	[[nodiscard]] auto save_file(std::filesystem::path, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>
	{
		return detail::unsupported_result<uint64_t>(
			executor_, std::forward<Token>(token));
	}

	[[nodiscard]] std::span<const std::byte> buffered_body() const noexcept {
		return buffered_body_;
	}
	void cancel() noexcept {}

private:
	executor_type executor_;
	protocol protocol_ = protocol::v1_1;
	response_head head_ {};
	std::unique_ptr<body_reader_type> body_;
	std::vector<std::byte> buffered_body_;
};

using response = basic_response<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_RESPONSE_H
