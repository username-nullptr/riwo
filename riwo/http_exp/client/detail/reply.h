// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_REPLY_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_REPLY_H

#include <fstream>

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_reply<Exec>::impl
{
public:
	impl(executor_type executor_value, protocol protocol_value,
		protocol response_protocol_value, response_head head_value,
		std::shared_ptr<detail::body_reader_state> state,
		std::vector<std::byte> buffered_body_value,
		std::shared_ptr<void> owner_value, uint64_t max_buffered_body_value) :
		executor(std::move(executor_value)), protocol_value(protocol_value),
		response_protocol_value(response_protocol_value),
		head(std::move(head_value)),
		body(std::make_unique<body_reader_type>(executor, std::move(state))),
		buffered_body(std::move(buffered_body_value)),
		owner(std::move(owner_value)),
		max_buffered_body(max_buffered_body_value)
	{
	}

	executor_type executor;
	protocol protocol_value;
	protocol response_protocol_value;
	response_head head;
	std::unique_ptr<body_reader_type> body;
	std::vector<std::byte> buffered_body;
	std::shared_ptr<void> owner;
	uint64_t max_buffered_body;
};

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply()
	requires std::default_initializable<executor_type> :
	basic_reply(executor_type{})
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_reply<Exec>::basic_reply(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_type> :
	basic_reply(std::forward<Exec0>(executor), protocol::v1_1, {},
		detail::make_body_reader_state())
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_reply<Exec>::basic_reply(Exec0 &&executor,
	protocol selected_protocol, response_head head,
	std::shared_ptr<detail::body_reader_state> body,
		std::vector<std::byte> buffered_body, std::shared_ptr<void> owner,
		uint64_t max_buffered_body, protocol response_protocol)
	requires core_concepts::match_sched<Exec0,executor_type> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)), selected_protocol,
		response_protocol, std::move(head), std::move(body),
		std::move(buffered_body), std::move(owner), max_buffered_body))
{
}

template <core_concepts::exec Exec>
basic_reply<Exec>::~basic_reply() = default;

template <core_concepts::exec Exec>
auto basic_reply<Exec>::get_executor() const noexcept -> executor_type {
	return m_impl->executor;
}

template <core_concepts::exec Exec>
protocol basic_reply<Exec>::selected_protocol() const noexcept {
	return m_impl->protocol_value;
}

template <core_concepts::exec Exec>
protocol basic_reply<Exec>::http_version() const noexcept {
	return m_impl->response_protocol_value;
}

template <core_concepts::exec Exec>
http_exp::status basic_reply<Exec>::status() const noexcept {
	return m_impl->head.status;
}

template <core_concepts::exec Exec>
const fields &basic_reply<Exec>::headers() const noexcept {
	return m_impl->head.headers;
}

template <core_concepts::exec Exec>
auto basic_reply<Exec>::body() noexcept -> body_reader_type& {
	return *m_impl->body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_reply<Exec>::read_all(Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,std::vector<std::byte>>
{
	const auto limit = m_impl->max_buffered_body;
	if constexpr( is_error_code_token_v<Token> || is_sync_opt_token_v<Token> )
	{
		std::vector<std::byte> result;
		std::array<std::byte,8192> buffer;
		error_code error;
		for(;;)
		{
			auto read = body().read_some(riwo::buffer(buffer), error);
			if( error ) break;
			if( read.bytes > limit - result.size() )
			{
				error = make_error_code(errc::buffer_limit_exceeded);
				break;
			}
			result.insert(result.end(), buffer.begin(), buffer.begin() +
				static_cast<std::ptrdiff_t>(read.bytes));
			if( read.eof ) break;
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
				sys_expected<std::vector<std::byte>>(std::move(result)));
		}
	}
	else
	{
		struct operation : std::enable_shared_from_this<operation>
		{
			body_reader_type *reader;
			std::vector<std::byte> result;
			std::array<std::byte,8192> buffer;
			uint64_t limit;
			asio::any_completion_handler<void(error_code,std::vector<std::byte>)>
				handler;

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
						return;
					}
					if( read.bytes > self->limit - self->result.size() )
					{
						std::move(self->handler)(
							make_error_code(errc::buffer_limit_exceeded),
							std::vector<std::byte>{});
						return;
					}
					self->result.insert(self->result.end(), self->buffer.begin(),
						self->buffer.begin() +
							static_cast<std::ptrdiff_t>(read.bytes));
					if( read.eof )
					{
						std::move(self->handler)(error_code{},
							std::move(self->result));
						return;
					}
					self->start();
				});
			}
		};
		return initiate_io<std::vector<std::byte>>(m_impl->executor,
		[state = m_impl](auto handler) mutable {
			auto value = std::make_shared<operation>();
			value->reader = state->body.get();
			value->limit = state->max_buffered_body;
			value->handler = decltype(value->handler)(std::move(handler));
			value->start();
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_reply<Exec>::save_file(std::filesystem::path path, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>
{
	if constexpr( is_error_code_token_v<Token> || is_sync_opt_token_v<Token> )
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		error_code error;
		uint64_t total = 0;
		if( !stream )
			error = make_system_error_code(std::errc::io_error);
		std::array<std::byte,8192> buffer;
		while( !error )
		{
			auto read = body().read_some(riwo::buffer(buffer), error);
			if( error ) break;
			stream.write(reinterpret_cast<const char*>(buffer.data()),
				static_cast<std::streamsize>(read.bytes));
			if( !stream )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
			total += read.bytes;
			if( read.eof ) break;
		}
		if constexpr( is_error_code_token_v<Token> )
		{
			token = error;
			return total;
		}
		else
		{
			return expected_value_or_throw(error ?
				sys_expected<uint64_t>(sys_unexpected(error)) :
				sys_expected<uint64_t>(total));
		}
	}
	else
	{
		struct operation : std::enable_shared_from_this<operation>
		{
			body_reader_type *reader;
			std::ofstream stream;
			std::array<std::byte,8192> buffer;
			uint64_t total = 0;
			asio::any_completion_handler<void(error_code,uint64_t)> handler;

			void start()
			{
				if( !stream )
				{
					std::move(handler)(make_system_error_code(std::errc::io_error), 0);
					return;
				}
				auto self = this->shared_from_this();
				reader->read_some(riwo::buffer(buffer),
				[self = std::move(self)]
				(error_code error, body_read_result read) mutable
				{
					if( error )
					{
						std::move(self->handler)(error, self->total);
						return;
					}
					self->stream.write(
						reinterpret_cast<const char*>(self->buffer.data()),
						static_cast<std::streamsize>(read.bytes));
					if( !self->stream )
					{
						std::move(self->handler)(
							make_system_error_code(std::errc::io_error), self->total);
						return;
					}
					self->total += read.bytes;
					if( read.eof )
					{
						std::move(self->handler)(error_code{}, self->total);
						return;
					}
					self->start();
				});
			}
		};
		return initiate_io<uint64_t>(m_impl->executor,
		[state = m_impl, path = std::move(path)](auto handler) mutable {
			auto value = std::make_shared<operation>();
			value->reader = state->body.get();
			value->stream.open(path, std::ios::binary | std::ios::trunc);
			value->handler = decltype(value->handler)(std::move(handler));
			value->start();
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
std::span<const std::byte>
basic_reply<Exec>::buffered_body() const noexcept {
	return m_impl->buffered_body;
}

template <core_concepts::exec Exec>
void basic_reply<Exec>::cancel() noexcept {
	if( m_impl->body ) m_impl->body->cancel();
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_REPLY_H
