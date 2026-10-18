// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_WRITER_H
#define RIWO_HTTP_EXP_BODY_WRITER_H

#include <riwo/http_exp/body/detail/unsupported.h>
#include <riwo/http_exp/message/fields.h>

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_body_writer
{
	RIWO_DISABLE_COPY_MOVE(basic_body_writer)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	template <typename Exec0>
	explicit basic_body_writer(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type> :
		executor_(get_executor_helper(std::forward<Exec0>(executor)))
	{
	}

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
	executor_type executor_;
};

using body_writer = basic_body_writer<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_WRITER_H
