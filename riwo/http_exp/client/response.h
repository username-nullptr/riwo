// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_RESPONSE_H
#define RIWO_HTTP_EXP_CLIENT_RESPONSE_H

#include <riwo/http_exp/body/reader.h>
#include <riwo/http_exp/message/response_head.h>
#include <riwo/http_exp/protocol.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API response
{
public:
	using executor_type = asio::any_io_executor;

	response();
	~response();
	response(const response &) = delete;
	response &operator=(const response &) = delete;
	response(response &&) noexcept;
	response &operator=(response &&) noexcept;

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] http_exp::status status() const noexcept;
	[[nodiscard]] const fields &headers() const noexcept;
	[[nodiscard]] body_reader &body() noexcept;

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

	[[nodiscard]] std::span<const std::byte> buffered_body() const noexcept;
	void cancel() noexcept;

private:
	executor_type executor_ {};
	protocol protocol_ = protocol::v1_1;
	response_head head_ {};
	std::unique_ptr<body_reader> body_;
	std::vector<std::byte> buffered_body_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_RESPONSE_H
