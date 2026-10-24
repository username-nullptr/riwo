// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_READER_H
#define RIWO_HTTP_EXP_BODY_READER_H

#include <riwo/http_exp/protocol/fields.h>

namespace riwo::http_exp { namespace detail {
class body_reader_state;
} //namespace detail

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
	using reader_state_ptr = std::shared_ptr<detail::body_reader_state>;

public:
	basic_body_reader()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_body_reader(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_body_reader(reader_state_ptr state)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_body_reader(Exec0 &&executor, reader_state_ptr state)
		requires core_concepts::match_sched<Exec0,executor_t>;

	~basic_body_reader();

public:
	template <core_concepts::dis_detached_tf_opt_token<error_code,body_read_result> Token = use_sync_t>
	[[nodiscard]] auto read_some(mutable_buffer buffer, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto discard(Token &&token = {});

	template <core_concepts::dis_detached_tf_opt_token<error_code,fields> Token = use_sync_t>
	[[nodiscard]] auto trailers(Token &&token = {});

	void cancel() noexcept;
	// Releases this facade without cancelling the shared body state. Used when
	// an exchange transfers ownership to a protocol-neutral duplex channel.
	void detach() noexcept;

	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] bool is_finished() const noexcept;

private:
	reader_state_ptr m_state {};
	executor_t m_exec {};
};

using body_reader = basic_body_reader<>;

} //namespace riwo::http_exp
#include <riwo/http_exp/body/detail/reader.h>


#endif //RIWO_HTTP_EXP_BODY_READER_H
