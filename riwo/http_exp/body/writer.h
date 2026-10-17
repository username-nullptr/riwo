// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_WRITER_H
#define RIWO_HTTP_EXP_BODY_WRITER_H

#include <riwo/http_exp/body/detail/unsupported.h>
#include <riwo/http_exp/message/fields.h>

namespace riwo::http_exp
{

class client_exchange;
class server_exchange;

class RIWO_HTTP_EXP_API body_writer
{
public:
	using executor_type = asio::any_io_executor;

	body_writer(const body_writer &) = delete;
	body_writer &operator=(const body_writer &) = delete;
	body_writer(body_writer &&) = delete;
	body_writer &operator=(body_writer &&) = delete;

	[[nodiscard]] executor_type get_executor() const noexcept { return executor_; }

	template <typename Token = use_sync_t>
	[[nodiscard]] auto write(const_buffer, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,size_t>
	{
		return detail::unsupported_result<size_t>(executor_, std::forward<Token>(token));
	}

	template <typename Token = use_sync_t>
	auto finish(fields = {}, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>
	{
		return detail::unsupported_operation(executor_, std::forward<Token>(token));
	}

	void cancel() noexcept {}

private:
	friend class client_exchange;
	friend class server_exchange;
	explicit body_writer(executor_type executor = {}) : executor_(std::move(executor)) {}
	executor_type executor_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_WRITER_H
