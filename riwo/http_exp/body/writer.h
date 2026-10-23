// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_WRITER_H
#define RIWO_HTTP_EXP_BODY_WRITER_H

#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp::detail
{
class body_writer_state;
}

namespace riwo::http_exp
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_body_writer
{
	RIWO_DISABLE_COPY_MOVE(basic_body_writer)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	basic_body_writer()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_body_writer(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_body_writer(
		std::shared_ptr<detail::body_writer_state> state)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_body_writer(Exec0 &&executor,
		std::shared_ptr<detail::body_writer_state> state)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_body_writer();

	[[nodiscard]] executor_t get_executor() const noexcept;

	// Asynchronous writes borrow the buffer until completion. Detached writes
	// make an owned copy because the caller has no completion lifetime barrier.
	template <typename Token = use_sync_t>
	[[nodiscard]] auto write(const_buffer buffer, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code,size_t>;

	template <typename Token = use_sync_t>
	auto finish(fields trailers = {}, Token &&token = {})
		requires core_concepts::tf_opt_token<Token,error_code>;

	void cancel() noexcept;
	// Releases this facade without cancelling the shared body state. Used when
	// an exchange transfers ownership to a protocol-neutral duplex channel.
	void detach() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using body_writer = basic_body_writer<>;

} //namespace riwo::http_exp

#include <riwo/http_exp/body/detail/writer.h>

#endif //RIWO_HTTP_EXP_BODY_WRITER_H
