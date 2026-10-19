// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_READER_H
#define RIWO_HTTP_EXP_BODY_READER_H

#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp::detail
{
class body_reader_state;
}

namespace riwo::http_exp
{

struct body_read_result
{
	size_t bytes = 0;
	bool eof = false;
};

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_body_reader
{
	RIWO_DISABLE_COPY_MOVE(basic_body_reader)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	template <typename Exec0>
	explicit basic_body_reader(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_type>;

	template <typename Exec0>
	basic_body_reader(Exec0 &&executor,
		std::shared_ptr<detail::body_reader_state> state)
		requires core_concepts::match_sched<Exec0,executor_type>;

	~basic_body_reader();

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] bool is_finished() const noexcept;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto read_some(mutable_buffer buffer, Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<
			Token,error_code,body_read_result>;

	template <typename Token = use_sync_t>
	auto discard(Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto trailers(Token &&token = {})
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,fields>;

	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using body_reader = basic_body_reader<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/body/detail/reader.h>

#endif //RIWO_HTTP_EXP_BODY_READER_H
