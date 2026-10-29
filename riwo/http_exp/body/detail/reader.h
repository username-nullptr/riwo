// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_READER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_READER_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp { namespace detail
{

class RIWO_HTTP_EXP_API body_reader_adapter
{
	RIWO_DISABLE_COPY_MOVE(body_reader_adapter)

public:
	using read_handler_t = asio::any_completion_handler<void(error_code,body_read_result)>;
	using trailers_handler_t = asio::any_completion_handler<void(error_code,fields)>;

	body_reader_adapter() = default;
	virtual ~body_reader_adapter();

public:
	[[nodiscard]] virtual body_read_result read_some_sync(mutable_buffer buffer, error_code &error) noexcept = 0;
	virtual void read_some(mutable_buffer buffer, read_handler_t handler) = 0;

	[[nodiscard]] virtual fields trailers_sync(error_code &error) noexcept = 0;
	virtual void trailers(trailers_handler_t handler) = 0;

	virtual void cancel() noexcept = 0;
};

using body_reader_adapter_ptr = std::shared_ptr<body_reader_adapter>;

class RIWO_HTTP_EXP_API body_reader_state final :
	public std::enable_shared_from_this<body_reader_state>
{
	RIWO_DISABLE_COPY_MOVE(body_reader_state)

public:
	using read_handler_t = body_reader_adapter::read_handler_t;
	using trailers_handler_t = body_reader_adapter::trailers_handler_t;

	using operation_handler_t = asio::any_completion_handler<void(error_code)>;
	using ptr_t = std::shared_ptr<body_reader_state>;

	explicit body_reader_state(body_reader_adapter_ptr adapter = {},
		bool ready = false, uint64_t max_discard = 1024 * 1024
	);
	body_reader_state(body_reader_adapter &adapter, bool ready,
		uint64_t max_discard = 1024 * 1024
	);
	~body_reader_state();

public:
	[[nodiscard]] body_read_result read_some(const mutable_buffer &buffer, error_code &error) noexcept;
	void read_some(const mutable_buffer &buffer, read_handler_t handler);

	void discard(error_code &error) noexcept;
	void discard(operation_handler_t handler);

	[[nodiscard]] fields trailers(error_code &error) noexcept;
	void trailers(trailers_handler_t handler);

	void cancel() noexcept;
	void bind_lifetime(std::weak_ptr<void> lifetime) noexcept;

	void set_ready(bool ready = true) noexcept;
	void set_finished(bool finished = true) noexcept;

	[[nodiscard]] bool is_finished() const noexcept;

private:
	class discard_operation;
	[[nodiscard]] std::shared_ptr<void> keep_alive() noexcept;

	body_reader_adapter_ptr m_owned_adapter {};
	body_reader_adapter *m_adapter = nullptr;
	std::weak_ptr<void> m_lifetime {};

	uint64_t m_max_discard = 0;
	optional<fields> m_trailer_cache {};

	bool m_ready = false;
	bool m_pending = false;

	bool m_finished = false;
	bool m_cancelled = false;

	bool m_parent_lifetime_bound = false;
};

using body_reader_state_ptr = body_reader_state::ptr_t;

[[nodiscard]] RIWO_HTTP_EXP_API
body_reader_state_ptr make_body_reader_state (
	body_reader_adapter_ptr adapter = {}, bool ready = false,
	uint64_t max_discard = 1024 * 1024
);

} //namespace detail

template <core_concepts::exec Exec>
basic_body_reader<Exec>::basic_body_reader() requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_body_reader(executor_t(riwo::get_executor()))
{

}

template <core_concepts::exec Exec>
basic_body_reader<Exec>::basic_body_reader(detail::body_reader_state_ptr state) requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_body_reader(executor_t(riwo::get_executor()), std::move(state))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_reader<Exec>::basic_body_reader(Exec0 &&executor) requires
	core_concepts::match_sched<Exec0,executor_t> :
	basic_body_reader(std::forward<Exec0>(executor), detail::make_body_reader_state())
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_reader<Exec>::basic_body_reader
(Exec0 &&executor, detail::body_reader_state_ptr state) requires
	core_concepts::match_sched<Exec0,executor_t> :
	m_state(state ? std::move(state) : detail::make_body_reader_state()),
	m_exec(get_executor_helper(std::forward<Exec0>(executor)))
{

}

template <core_concepts::exec Exec>
basic_body_reader<Exec>::~basic_body_reader()
{
	if( m_state and not m_state->is_finished() )
		m_state->cancel();
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,body_read_result> Token>
auto basic_body_reader<Exec>::read_some(mutable_buffer buffer, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		return m_state->read_some(buffer, token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_state->read_some(buffer, error);

		return expected_value_or_throw(error ?
			sys_expected<body_read_result>(sys_unexpected(error)) :
			sys_expected<body_read_result>(result)
		);
	}
	else
	{
		auto state = m_state;
		return initiate_io<body_read_result>(m_exec,
		[state = std::move(state), buffer](auto handler) mutable
		{
			state->read_some(buffer,
				detail::body_reader_state::read_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code> Token>
auto basic_body_reader<Exec>::discard(Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		m_state->discard(token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_state->discard(error);
		if( error )
		{
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error))
			);
		}
	}
	else
	{
		auto state = m_state;
		return initiate_io_void(m_exec,
		[state = std::move(state)](auto handler) mutable
		{
			state->discard (
				detail::body_reader_state::operation_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,fields> Token>
auto basic_body_reader<Exec>::trailers(Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		return m_state->trailers(token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto value = m_state->trailers(error);

		return expected_value_or_throw(error ?
			sys_expected<fields>(sys_unexpected(error)) :
			sys_expected<fields>(std::move(value))
		);
	}
	else
	{
		auto state = m_state;
		return initiate_io<fields>(m_exec,
		[state = std::move(state)](auto handler) mutable
		{
			state->trailers (
				detail::body_reader_state::trailers_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_body_reader<Exec>::cancel() noexcept
{
	m_state->cancel();
}

template <core_concepts::exec Exec>
void basic_body_reader<Exec>::detach() noexcept
{
	m_state = detail::make_body_reader_state();
}

template <core_concepts::exec Exec>
auto basic_body_reader<Exec>::get_executor() const noexcept -> executor_t
{
	return m_exec;
}

template <core_concepts::exec Exec>
bool basic_body_reader<Exec>::is_finished() const noexcept
{
	return m_state->is_finished();
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_DETAIL_READER_H
