// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_READER_H
#define RIWO_HTTP_EXP_BODY_READER_H

#include <riwo/http_exp/body/detail/unsupported.h>
#include <riwo/http_exp/message/fields.h>

namespace riwo::http_exp
{

class response;
class client_exchange;
class server_exchange;

class RIWO_HTTP_EXP_API body_reader
{
public:
	using executor_type = asio::any_io_executor;

	body_reader(const body_reader &) = delete;
	body_reader &operator=(const body_reader &) = delete;
	body_reader(body_reader &&) = delete;
	body_reader &operator=(body_reader &&) = delete;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }

	template <typename Token = use_sync_t>
	[[nodiscard]] auto read_some(mutable_buffer, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,size_t>
	{
		return detail::unsupported_result<size_t>(executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	auto discard(Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>
	{
		return detail::unsupported_operation(executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	[[nodiscard]] auto trailers(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,fields>
	{
		return detail::unsupported_result<fields>(executor_, std::forward<Token>(token));
	}

private:
	friend class response;
	friend class client_exchange;
	friend class server_exchange;
	explicit body_reader(executor_type executor = {}) : executor_(std::move(executor)) {}
	executor_type executor_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_READER_H
