// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_WRITER_H
#define RIWO_HTTP_EXP_BODY_WRITER_H

#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp { namespace detail  {
class body_writer_state;
} //namespace detail

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_body_writer
{
	RIWO_DISABLE_COPY_MOVE(basic_body_writer)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using writer_state_ptr = std::shared_ptr<detail::body_writer_state>;

public:
	basic_body_writer()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_body_writer(Exec0 &&exec)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_body_writer(writer_state_ptr state)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_body_writer(Exec0 &&exec, writer_state_ptr state)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_body_writer();

public:
	// Asynchronous writes borrow the buffer until completion. Detached writes
	// make an owned copy because the caller has no completion lifetime barrier.
	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	[[nodiscard]] auto write(const_buffer buffer, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto finish(fields trailers = {}, Token &&token = {});

	void cancel() noexcept;
	// Releases this facade without cancelling the shared body state. Used when
	// an exchange transfers ownership to a protocol-neutral duplex channel.
	void detach() noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;

private:
	writer_state_ptr m_state {};
	executor_t m_exec {};
};

using body_writer = basic_body_writer<>;

} //namespace riwo::http_exp
#include <riwo/http_exp/body/detail/writer.h>


#endif //RIWO_HTTP_EXP_BODY_WRITER_H
