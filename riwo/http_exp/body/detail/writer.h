// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API body_writer_adapter
{
	RIWO_DISABLE_COPY_MOVE(body_writer_adapter)

public:
	using write_handler = asio::any_completion_handler<void(error_code,size_t)>;
	using finish_handler = asio::any_completion_handler<void(error_code)>;

	body_writer_adapter() = default;
	virtual ~body_writer_adapter();

	[[nodiscard]] virtual size_t write_sync(const_buffer buffer,
		error_code &error) noexcept = 0;
	virtual void write(const_buffer buffer, write_handler handler) = 0;
	virtual void finish_sync(fields trailers, error_code &error) noexcept = 0;
	virtual void finish(fields trailers, finish_handler handler) = 0;
	virtual void cancel() noexcept = 0;
};

class RIWO_HTTP_EXP_API body_writer_state final :
	public std::enable_shared_from_this<body_writer_state>
{
	RIWO_DISABLE_COPY_MOVE(body_writer_state)

public:
	using write_handler = body_writer_adapter::write_handler;
	using finish_handler = body_writer_adapter::finish_handler;

	explicit body_writer_state(std::shared_ptr<body_writer_adapter> adapter = {},
		body_spec spec = body_spec::empty(), bool ready = false);
	~body_writer_state();

	[[nodiscard]] size_t write(const_buffer buffer, error_code &error) noexcept;
	void write(const_buffer buffer, write_handler handler);
	void finish(fields trailers, error_code &error) noexcept;
	void finish(fields trailers, finish_handler handler);
	void cancel() noexcept;
	void configure(body_spec spec, bool ready = false) noexcept;
	void set_ready(bool ready = true) noexcept;
	[[nodiscard]] bool ended() const noexcept;
	[[nodiscard]] uint64_t accepted_size() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<body_writer_state> make_body_writer_state(
	std::shared_ptr<body_writer_adapter> adapter = {},
	body_spec spec = body_spec::empty(), bool ready = false);

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_body_writer<Exec>::impl
{
public:
	impl(executor_type executor_value,
		std::shared_ptr<detail::body_writer_state> state_value) :
		m_executor(std::move(executor_value)), m_state(std::move(state_value))
	{
	}

	executor_type m_executor;
	std::shared_ptr<detail::body_writer_state> m_state;
};

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_writer<Exec>::basic_body_writer(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_body_writer(std::forward<Exec0>(executor),
		detail::make_body_writer_state())
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_body_writer<Exec>::basic_body_writer(Exec0 &&executor,
	std::shared_ptr<detail::body_writer_state> state)
	requires core_concepts::match_sched<Exec0,executor_type> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)),
		state ? std::move(state) : detail::make_body_writer_state()))
{
}

template <core_concepts::exec Exec>
basic_body_writer<Exec>::~basic_body_writer() {
	if( m_impl->m_state and not m_impl->m_state->ended() ) m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
auto basic_body_writer<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->m_executor;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_body_writer<Exec>::write(const_buffer buffer, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
		return m_impl->m_state->write(buffer, token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto size = m_impl->m_state->write(buffer, error);
		return expected_value_or_throw(error ?
			sys_expected<size_t>(sys_unexpected(error)) : sys_expected<size_t>(size));
	}
	else
	{
		auto state = m_impl->m_state;
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owned = std::make_shared<std::vector<std::byte>>(buffer.size());
			if( buffer.size() != 0 )
				std::memcpy(owned->data(), buffer.data(), buffer.size());
			return initiate_io<size_t>(m_impl->m_executor,
			[state = std::move(state), owned](auto handler) mutable {
				state->write(riwo::buffer(*owned),
					typename detail::body_writer_state::write_handler(
					[owned, handler = std::move(handler)]
					(error_code error, size_t size) mutable {
						std::move(handler)(error, size);
					}));
			}, std::forward<Token>(token));
		}
		else
		{
			return initiate_io<size_t>(m_impl->m_executor,
			[state = std::move(state), buffer](auto handler) mutable {
				state->write(buffer,
					typename detail::body_writer_state::write_handler(
						std::move(handler)));
			}, std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_body_writer<Exec>::finish(fields trailers, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		m_impl->m_state->finish(std::move(trailers), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->m_state->finish(std::move(trailers), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = m_impl->m_state;
		return initiate_io_void(m_impl->m_executor,
		[state = std::move(state), trailers = std::move(trailers)]
		(auto handler) mutable {
			state->finish(std::move(trailers),
				typename detail::body_writer_state::finish_handler(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_body_writer<Exec>::cancel() noexcept {
	m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
void basic_body_writer<Exec>::detach() noexcept {
	m_impl->m_state = detail::make_body_writer_state();
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_DETAIL_WRITER_H
