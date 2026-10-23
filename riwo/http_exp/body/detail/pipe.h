// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_PIPE_H
#define RIWO_HTTP_EXP_BODY_DETAIL_PIPE_H

namespace riwo::http_exp::detail
{

template <core_concepts::exec Exec, typename Sink, typename Token>
requires concepts::body_sink_model<std::remove_cvref_t<Sink>> and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>
[[nodiscard]] auto pipe_body(basic_body_reader<Exec> &reader, Sink &&sink,
	std::shared_ptr<void> owner, Token &&token)
{
	RIWO_UNUSED(owner);
	using sink_t = std::remove_cvref_t<Sink>;
	if constexpr( is_error_code_token_v<Token> or is_sync_opt_token_v<Token> )
	{
		sink_t output(std::forward<Sink>(sink));
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;
		error_code error;
		if constexpr( requires(sink_t &value, error_code &result) {
			{ value.prepare(result) } -> std::same_as<void>;
		})
			output.prepare(error);
		for(;;)
		{
			if( error ) break;
			auto read = reader.read_some(riwo::buffer(buffer), error);
			if( error ) break;
			size_t offset = 0;
			while( offset != read.bytes )
			{
				const auto written = output.write(const_buffer(
					buffer.data() + offset, read.bytes - offset), error);
				if( written > read.bytes - offset )
				{
					error = make_system_error_code(std::errc::io_error);
					break;
				}
				if( written > std::numeric_limits<uint64_t>::max() - total )
				{
					error = make_system_error_code(std::errc::value_too_large);
					break;
				}
				offset += written;
				total += written;
				if( error or written == 0 )
				{
					if( not error )
						error = make_system_error_code(std::errc::io_error);
					break;
				}
			}
			if( error or read.eof ) break;
			if( read.bytes == 0 )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
		}
		if( error ) output.cancel();
		else output.finish(error);
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
			basic_body_reader<Exec> *reader = nullptr;
			sink_t sink;
			std::shared_ptr<void> owner;
			std::array<std::byte,8192> buffer;
			size_t chunk_size = 0;
			size_t chunk_offset = 0;
			uint64_t total = 0;
			bool eof = false;
			asio::any_completion_handler<void(error_code,uint64_t)> handler;

			explicit operation(sink_t value) : sink(std::move(value)) {}

			void start()
			{
				if constexpr( requires(sink_t &value, error_code &result) {
					{ value.prepare(result) } -> std::same_as<void>;
				})
				{
					error_code error;
					sink.prepare(error);
					if( error )
					{
						fail(error);
						return;
					}
				}
				start_read();
			}

			void start_read()
			{
				auto self = this->shared_from_this();
				reader->read_some(riwo::buffer(buffer),
				[self = std::move(self)]
				(error_code error, body_read_result read) mutable
				{
					if( error )
					{
						self->fail(error);
						return;
					}
					self->chunk_size = read.bytes;
					self->chunk_offset = 0;
					self->eof = read.eof;
					if( read.bytes == 0 )
					{
						if( read.eof ) self->start_finish();
						else self->fail(make_system_error_code(std::errc::io_error));
						return;
					}
					self->start_write();
				});
			}

			void start_write()
			{
				auto self = this->shared_from_this();
				sink.write(const_buffer(buffer.data() + chunk_offset,
					chunk_size - chunk_offset),
				[self = std::move(self)](error_code error, size_t size) mutable
				{
					self->write_complete(error, size);
				});
			}

			void write_complete(error_code error, size_t size)
			{
				const auto remaining = chunk_size - chunk_offset;
				if( size > remaining )
				{
					fail(make_system_error_code(std::errc::io_error));
					return;
				}
				if( size > std::numeric_limits<uint64_t>::max() - total )
				{
					fail(make_system_error_code(std::errc::value_too_large));
					return;
				}
				chunk_offset += size;
				total += size;
				if( error )
				{
					fail(error);
					return;
				}
				if( size == 0 )
				{
					fail(make_system_error_code(std::errc::io_error));
					return;
				}
				if( chunk_offset != chunk_size )
				{
					start_write();
					return;
				}
				if( eof ) start_finish();
				else start_read();
			}

			void start_finish()
			{
				auto self = this->shared_from_this();
				sink.finish([self = std::move(self)](error_code error) mutable {
					self->complete(error);
				});
			}

			void fail(error_code error)
			{
				sink.cancel();
				complete(error);
			}

			void complete(error_code error) {
				std::move(handler)(error, total);
			}
		};

		return initiate_io<uint64_t>(reader.get_executor(),
		[reader = &reader, sink = sink_t(std::forward<Sink>(sink)),
		 owner = std::move(owner)](auto handler) mutable {
			auto value = std::make_shared<operation>(std::move(sink));
			value->reader = reader;
			value->owner = std::move(owner);
			value->handler = decltype(value->handler)(std::move(handler));
			value->start();
		}, std::forward<Token>(token));
	}
}

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec, typename Sink, typename Token>
requires concepts::body_sink_model<std::remove_cvref_t<Sink>> and
	core_concepts::dis_detached_tf_opt_token<Token,error_code,uint64_t>
auto pipe(basic_body_reader<Exec> &reader, Sink &&sink, Token &&token)
{
	return detail::pipe_body(reader, std::forward<Sink>(sink), {},
		std::forward<Token>(token));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_DETAIL_PIPE_H
