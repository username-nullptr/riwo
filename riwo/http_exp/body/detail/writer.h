// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp { namespace detail
{

class RIWO_HTTP_EXP_API body_writer_adapter
{
	RIWO_DISABLE_COPY_MOVE(body_writer_adapter)

public:
	using write_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using finish_handler_t = asio::any_completion_handler<void(error_code)>;

	body_writer_adapter() = default;
	virtual ~body_writer_adapter();

public:
	[[nodiscard]] virtual size_t write_sync(const_buffer buffer, error_code &error) noexcept = 0;
	virtual void write(const_buffer buffer, write_handler_t handler) = 0;

	virtual void finish_sync(fields trailers, error_code &error) noexcept = 0;
	virtual void finish(fields trailers, finish_handler_t handler) = 0;

	virtual void cancel() noexcept = 0;
};

using body_writer_adapter_ptr = std::shared_ptr<body_writer_adapter>;

class RIWO_HTTP_EXP_API body_writer_state final :
	public std::enable_shared_from_this<body_writer_state>
{
	RIWO_DISABLE_COPY_MOVE(body_writer_state)

public:
	using write_handler_t = body_writer_adapter::write_handler_t;
	using finish_handler_t = body_writer_adapter::finish_handler_t;
	using ptr_t = std::shared_ptr<body_writer_state>;

	explicit body_writer_state(std::shared_ptr<body_writer_adapter> adapter = {},
		body_spec spec = body_spec::empty(), bool ready = false
	);
	body_writer_state(body_writer_adapter &adapter,
		body_spec spec = body_spec::empty(), bool ready = false
	);
	~body_writer_state();

	void bind_lifetime(std::weak_ptr<void> lifetime) noexcept;

public:
	[[nodiscard]] size_t write(const const_buffer &buffer, error_code &error) noexcept;
	void write(const const_buffer &buffer, write_handler_t handler);

	void finish(fields trailers, error_code &error) noexcept;
	void finish(fields trailers, finish_handler_t handler);
	void cancel() noexcept;

	void set_spec(body_spec spec, bool ready = false) noexcept;
	void set_ready(bool ready = true) noexcept;

	[[nodiscard]] bool ended() const noexcept;
	[[nodiscard]] uint64_t accepted_size() const noexcept;

private:
	[[nodiscard]] std::shared_ptr<void> keep_alive() noexcept;

	body_writer_adapter_ptr m_owned_adapter {};
	body_writer_adapter *m_adapter = nullptr;

	std::weak_ptr<void> m_lifetime {};
	body_spec m_spec;

	uint64_t m_accepted = 0;

	bool m_ready = false;
	bool m_pending = false;

	bool m_ended = false;
	bool m_terminated = false;

	bool m_parent_lifetime_bound = false;
};

using body_writer_state_ptr = body_writer_state::ptr_t;

[[nodiscard]] RIWO_HTTP_EXP_API
body_writer_state_ptr make_body_writer_state (
	body_writer_adapter_ptr adapter = {},
	body_spec spec = body_spec::empty(),
	bool ready = false
);

} //namespace detail

template <core_concepts::exec Exec>
basic_body_writer<Exec>::basic_body_writer()
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_body_writer(executor_t(riwo::get_executor()))
{

}

template <core_concepts::exec Exec>
basic_body_writer<Exec>::basic_body_writer(detail::body_writer_state_ptr state)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_body_writer(executor_t(riwo::get_executor()), std::move(state))
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_writer<Exec>::basic_body_writer(Exec0 &&exec)
	requires core_concepts::match_sched<Exec0,executor_t> :
	basic_body_writer(std::forward<Exec0>(exec), detail::make_body_writer_state())
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_writer<Exec>::basic_body_writer(Exec0 &&exec, detail::body_writer_state_ptr state)
	requires core_concepts::match_sched<Exec0,executor_t> :
	m_state(state ? std::move(state) : detail::make_body_writer_state()),
	m_exec(get_executor_helper(std::forward<Exec0>(exec)))
{

}

template <core_concepts::exec Exec>
basic_body_writer<Exec>::~basic_body_writer()
{
	if( m_state and not m_state->ended() )
		m_state->cancel();
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_body_writer<Exec>::write(const_buffer buffer, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		return m_state->write(buffer, token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto size = m_state->write(buffer, error);

		return expected_value_or_throw(error ?
			sys_expected<size_t>(sys_unexpected(error)) : sys_expected<size_t>(size)
		);
	}
	else
	{
		auto state = m_state;
		using token_t = std::remove_cvref_t<Token>;

		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owned = std::make_shared<std::vector<std::byte>>(buffer.size());
			if( buffer.size() != 0 )
				std::memcpy(owned->data(), buffer.data(), buffer.size());

			return initiate_io<size_t>(m_exec,
			[state = std::move(state), owned](auto handler) mutable
			{
				state->write(riwo::buffer(*owned),
					detail::body_writer_state::write_handler_t (
					[owned, handler = std::move(handler)](error_code error, size_t size) mutable {
						std::move(handler)(error, size);
					})
				);
			},
			std::forward<Token>(token));
		}
		else
		{
			return initiate_io<size_t>(m_exec,
			[state = std::move(state), buffer](auto handler) mutable
			{
				state->write(buffer,
					detail::body_writer_state::write_handler_t(std::move(handler))
				);
			},
			std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <core_concepts::tf_opt_token<error_code> Token>
auto basic_body_writer<Exec>::finish(fields trailers, Token &&token)
{
	if constexpr( is_error_code_token_v<Token> )
		m_state->finish(std::move(trailers), token);

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_state->finish(std::move(trailers), error);
		if( error )
		{
			(void)expected_value_or_throw (
				sys_expected<std::monostate>(sys_unexpected(error))
			);
		}
	}
	else
	{
		auto state = m_state;
		return initiate_io_void(m_exec,
		[state = std::move(state), trailers = std::move(trailers)](auto handler) mutable
		{
			state->finish(std::move(trailers),
				detail::body_writer_state::finish_handler_t(std::move(handler))
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_body_writer<Exec>::cancel() noexcept
{
	m_state->cancel();
}

template <core_concepts::exec Exec>
void basic_body_writer<Exec>::detach() noexcept
{
	m_state = detail::make_body_writer_state();
}

template <core_concepts::exec Exec>
auto basic_body_writer<Exec>::get_executor() const noexcept -> executor_t
{
	return m_exec;
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H
