// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_DETAIL_REPLY_H
#define RIWO_EXP_HTTP_CLIENT_DETAIL_REPLY_H

namespace riwo::experimental::http
{

template <core_concepts::exec Exec>
class RIWO_EXP_HTTP_TAPI basic_reply<Exec>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	struct operation : std::enable_shared_from_this<operation>
	{
		body_reader_t *reader = nullptr;
		uint64_t limit = 0;

		std::vector<std::byte> result {};
		std::array<std::byte,8192> buffer {};

		asio::any_completion_handler <
			void(error_code,std::vector<std::byte>)
		> handler {};

		void start()
		{
			auto self = this->shared_from_this();
			reader->read_some(riwo::buffer(buffer),
			[self = std::move(self)]
			(error_code error, body_read_result read) mutable
			{
				if( error )
				{
					std::move(self->handler)(error, std::vector<std::byte>{});
					return ;
				}
				if( read.bytes > self->limit - self->result.size() )
				{
					std::move(self->handler)(
						make_error_code(errc::buffer_limit_exceeded),
						std::vector<std::byte>{});
					return ;
				}
				self->result.insert(self->result.end(), self->buffer.begin(),
					self->buffer.begin() +
						static_cast<std::ptrdiff_t>(read.bytes));
				if( read.eof )
				{
					std::move(self->handler)(error_code{},
						std::move(self->result));
					return ;
				}
				self->start();
			});
		}
	};

public:
	impl(executor_t executor_value, protocol protocol_value, protocol reply_protocol_value,
		reply_head head_value, detail::body_reader_state_ptr state, std::vector<std::byte> buffered_body_value,
		std::shared_ptr<void> owner_value, uint64_t max_buffered_body_value
	) :
		m_exec(std::move(executor_value)), m_protocol_value(protocol_value),
		m_reply_protocol_value(reply_protocol_value), m_head(std::move(head_value)),
		m_body(m_exec, std::move(state)), m_buffered_body(std::move(buffered_body_value)),
		m_owner(std::move(owner_value)), m_max_buffered_body(max_buffered_body_value) {}

public:
	executor_t m_exec {};
	protocol m_protocol_value {};
	protocol m_reply_protocol_value {};

	reply_head m_head {};
	body_reader_t m_body;
	std::vector<std::byte> m_buffered_body {};

	std::shared_ptr<void> m_owner {};
	uint64_t m_max_buffered_body = 0;
};

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply
(protocol selected_protocol, reply_head head, detail::body_reader_state_ptr body,
	std::vector<std::byte> buffered_body, std::shared_ptr<void> owner,
	uint64_t max_buffered_body, protocol reply_protocol)
requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_reply(executor_t(riwo::get_executor()), selected_protocol, std::move(head), std::move(body),
		std::move(buffered_body), std::move(owner), max_buffered_body, reply_protocol)
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_reply<Exec>::basic_reply(Exec0 &&executor) requires
	core_concepts::match_sched<Exec0,executor_t> :
	basic_reply(std::forward<Exec0>(executor), protocol::v1_1, {}, detail::make_body_reader_state())
{

}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_reply<Exec>::basic_reply
(Exec0 &&executor, protocol selected_protocol, reply_head head, detail::body_reader_state_ptr body,
	std::vector<std::byte> buffered_body, std::shared_ptr<void> owner,
	uint64_t max_buffered_body, protocol reply_protocol)
requires
	core_concepts::match_sched<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)), selected_protocol, reply_protocol,
		std::move(head), std::move(body), std::move(buffered_body), std::move(owner), max_buffered_body
	))
{

}

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply() requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	basic_reply(executor_t(riwo::get_executor()))
{

}

template <core_concepts::exec Exec>
basic_reply<Exec>::~basic_reply() = default;

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply(basic_reply &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_shared<impl>(m_impl->m_exec,
		protocol::v1_1, protocol::v1_1, reply_head{},
		detail::make_body_reader_state(), std::vector<std::byte>{},
		std::shared_ptr<void>{}, 8U * 1024U * 1024U
	);
}

template <core_concepts::exec Exec>
auto basic_reply<Exec>::operator=(basic_reply &&other) noexcept -> basic_reply&
{
	if( this == &other )
		return *this;

	auto replacement = std::make_shared<impl>(other.m_impl->m_exec,
		protocol::v1_1, protocol::v1_1, reply_head{},
		detail::make_body_reader_state(), std::vector<std::byte>{},
		std::shared_ptr<void>{}, 8U * 1024U * 1024U
	);
	m_impl = std::move(other.m_impl);
	other.m_impl = std::move(replacement);
	return *this;
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,std::vector<std::byte>> Token>
auto basic_reply<Exec>::read_all(Token &&token)
{
	const auto limit = m_impl->m_max_buffered_body;
	if constexpr( is_error_code_token_v<Token> or is_sync_opt_token_v<Token> )
	{
		std::vector<std::byte> result;
		std::array<std::byte,8192> buffer {};
		error_code error {};
		for(;;)
		{
			auto read = body().read_some(riwo::buffer(buffer), error);
			if( error )
				break;

			if( read.bytes > limit - result.size() )
			{
				error = make_error_code(errc::buffer_limit_exceeded);
				break;
			}
			result.insert(result.end(), buffer.begin(),
				buffer.begin() + static_cast<std::ptrdiff_t>(read.bytes)
			);
			if( read.eof )
				break;
		}
		if constexpr( is_error_code_token_v<Token> )
		{
			token = error;
			return result;
		}
		else
		{
			return expected_value_or_throw(error ?
				sys_expected<std::vector<std::byte>>(sys_unexpected(error)) :
				sys_expected<std::vector<std::byte>>(std::move(result))
			);
		}
	}
	else
	{
		return initiate_io<std::vector<std::byte>>(m_impl->m_exec,
		[state = m_impl](auto handler) mutable
		{
			auto value = std::make_shared<typename impl::operation>();
			value->reader = std::addressof(state->m_body);
			value->limit = state->m_max_buffered_body;
			value->handler = decltype(value->handler)(std::move(handler));
			value->start();
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token>
auto basic_reply<Exec>::save_file(const std::filesystem::path &path, Token &&token)
{
	return detail::pipe_body(body(),
		file_sink(path), m_impl, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
template <typename File, core_concepts::dis_detached_tf_opt_token<error_code,uint64_t> Token>
auto basic_reply<Exec>::save_file(File &&file, Token &&token) requires
	(not std::same_as<std::remove_cvref_t<File>,std::filesystem::path>) and
	std::constructible_from<file_sink,File>
{
	return detail::pipe_body(body(),
		file_sink(std::forward<File>(file)), m_impl,
		std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
void basic_reply<Exec>::cancel() noexcept
{
	m_impl->m_body.cancel();
}

template <core_concepts::exec Exec>
status basic_reply<Exec>::status() const noexcept
{
	return m_impl->m_head.status;
}

template <core_concepts::exec Exec>
const fields &basic_reply<Exec>::headers() const noexcept
{
	return m_impl->m_head.headers;
}

template <core_concepts::exec Exec>
auto basic_reply<Exec>::body() noexcept -> body_reader_t&
{
	return m_impl->m_body;
}

template <core_concepts::exec Exec>
std::span<const std::byte> basic_reply<Exec>::buffered_body() const noexcept
{
	return m_impl->m_buffered_body;
}

template <core_concepts::exec Exec>
protocol basic_reply<Exec>::selected_protocol() const noexcept
{
	return m_impl->m_protocol_value;
}

template <core_concepts::exec Exec>
protocol basic_reply<Exec>::http_version() const noexcept
{
	return m_impl->m_reply_protocol_value;
}

template <core_concepts::exec Exec>
auto basic_reply<Exec>::get_executor() const noexcept -> executor_t
{
	return m_impl->m_exec;
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_CLIENT_DETAIL_REPLY_H
