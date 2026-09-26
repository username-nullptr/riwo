// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_REPLY_H
#define RIWO_HTTP_CLIENT_DETAIL_REPLY_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_reply<Exec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(lease_ptr lease) :
		m_lease(std::move(lease)),
		m_exec(lease_executor(m_lease)) {}

	impl(lease_ptr lease, parser_t &&parser) :
		m_lease(std::move(lease)),
		m_exec(lease_executor(m_lease)),
		m_parser(std::move(parser)) {}

private:
	[[nodiscard]] static executor_t lease_executor(const lease_ptr &lease)
	{
		if( not lease or not lease->is_valid() )
			invalid_argument::loc_throw("reply connection lease is invalid");
		return lease->get().get_executor();
	}

public:
	void capture_cookies()
	{
		if( m_cookies_captured or not m_cookie_jar or
			m_parser.status() == status::none or
			m_parser.stage() == parser_t::stage_t::header or
			m_parser.is_informational() )
			return ;

		m_cookie_jar->store(m_origin, m_parser.set_cookies());
		m_cookies_captured = true;
	}

	[[nodiscard]] sys_expected<status_enum> wait() noexcept
	{
		if( m_first_error )
			return sys_unexpected(m_first_error);

		else if( m_parser.stage() != parser_t::stage_t::header )
		{
			if( m_parser.is_informational() )
			{
				auto expected = m_parser.next_message();
				if( not expected )
				{
					close_connection();
					return sys_unexpected(expected.error());
				}
				if( *expected )
				{
					capture_cookies();
					finish_connection();
					return m_parser.status();
				}
			}
			else
			{
				capture_cookies();
				finish_connection();
				return m_parser.status();
			}
		}
		if( not has_connection() )
		{
			return sys_unexpected(
				make_system_error_code(std::errc::not_connected)
			);
		}
		auto &conn = connection();
		if( not conn.is_open() )
		{
			close_connection();
			return sys_unexpected (
				make_system_error_code(std::errc::not_connected)
			);
		}
		using namespace riwo::operators;
		error_code error;

		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size];
		for(;;)
		{
			auto sum = conn.read(buffer(buf, buf_size), error);
			if( error )
			{
				close_connection();
				return sys_unexpected(error);
			}
			auto expected = m_parser.append({buf, sum});
			if( not expected )
			{
				close_connection();
				return sys_unexpected(expected.error());
			}
			else if( *expected )
				break;
		}
		capture_cookies();
		finish_connection();
		return m_parser.status();
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,status_enum>,executor_t>
	co_wait(std::shared_ptr<impl> self)
	{
		if( self->m_first_error )
		{
			co_return std::tuple<error_code,status_enum> {
				self->m_first_error, status_enum{}
			};
		}
		if( self->m_parser.stage() != parser_t::stage_t::header )
		{
			if( self->m_parser.is_informational() )
			{
				auto expected = self->m_parser.next_message();
				if( not expected )
				{
					self->close_connection();
					co_return std::tuple<error_code,status_enum> {
						expected.error(), status_enum{}
					};
				}
				if( *expected )
				{
					self->capture_cookies();
					self->finish_connection();

					co_return std::tuple<error_code,status_enum> {
						error_code{}, self->m_parser.status()
					};
				}
			}
			else
			{
				self->capture_cookies();
				self->finish_connection();

				co_return std::tuple<error_code,status_enum> {
					error_code{}, self->m_parser.status()
				};
			}
		}
		if( not self->has_connection() )
		{
			co_return std::tuple {
				make_system_error_code(std::errc::not_connected), status_enum{}
			};
		}
		auto &conn = self->connection();
		if( not conn.is_open() )
		{
			self->close_connection();
			co_return std::tuple {
				make_system_error_code(std::errc::not_connected), status_enum{}
			};
		}
		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size];
		for(;;)
		{
			auto [error, sum] = co_await conn.read (
				buffer(buf, buf_size), asio::as_tuple(deferred)
			);
			if( error )
			{
				self->close_connection();
				co_return std::tuple<error_code,status_enum> {
					error, status_enum{}
				};
			}
			auto expected = self->m_parser.append({buf, sum});
			if( not expected )
			{
				self->close_connection();
				co_return std::tuple<error_code,status_enum> {
					expected.error(), status_enum{}
				};
			}
			if( *expected )
				break;
		}
		self->capture_cookies();
		self->finish_connection();

		co_return std::tuple<error_code,status_enum> {
			error_code{}, self->m_parser.status()
		};
	}

	template <typename Token>
	[[nodiscard]] auto async_wait(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,status_enum)>(
		[exec, self = this->shared_from_this()]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_wait(std::move(self)),
				riwo::detail::co_spawn_io_handler<status_enum,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] io_expected read(const mutable_buffer &buf) noexcept
	{
		if( m_first_error )
			return sys_unexpected(m_first_error);

		if( m_parser.stage() == parser_t::stage_t::finished )
		{
			finish_connection();
			return sys_unexpected (
				make_error_code(errc::eof)
			);
		}
		if( m_parser.stage() == parser_t::stage_t::header )
		{
			if( auto expected = wait(); not expected )
				return io_unexpected(expected.error());
		}
		if( m_parser.stage() == parser_t::stage_t::finished )
		{
			finish_connection();
			return io_unexpected(make_error_code(errc::eof));
		}
		if( not has_connection() )
		{
			return io_unexpected (
				make_system_error_code(std::errc::not_connected)
			);
		}
		auto &conn = connection();
		if( not conn.is_open() )
		{
			close_connection();
			return io_unexpected (
				make_system_error_code(std::errc::not_connected)
			);
		}
		size_t sum = 0;
		if( buf.size() == 0 )
			return sum;

		auto state = conn.options();
		if( not state )
		{
			close_connection();
			return io_unexpected(state.error());
		}
		auto read_size = state->receive_buffer_size;
		if( read_size == 0 )
			read_size = 0xFFFF;

		auto dst_buf = static_cast<char*>(buf.data());
		for(;;)
		{
			sum += m_parser.read_partial_body (
				{dst_buf + sum, buf.size() - sum}
			);
			if( sum == buf.size() or m_parser.stage() == parser_t::stage_t::finished )
				break;

			if( auto direct_size = m_parser.prepare_direct_body_read(buf.size() - sum) )
			{
				error_code error {};
				auto bytes = conn.read({dst_buf + sum, direct_size}, error);
				if( error )
				{
					close_connection();
					return sum > 0 ? io_expected(sum) : io_unexpected(error);
				}
				if( not m_parser.commit_direct_body_read(bytes) )
				{
					close_connection();
					return io_unexpected(make_system_error_code(std::errc::protocol_error));
				}
				sum += bytes;
				continue;
			}
			std::string body(read_size,'\0');
			for(;;)
			{
				error_code error {};
				auto tmp_sum = conn.read({body.data(), body.size()}, error);
				if( error )
				{
					close_connection();
					if( error == errc::eof and m_parser.finish_eof() )
					{
						error.clear();
						break;
					}
					return sum > 0 ? io_expected(sum) : io_unexpected(error);
				}
				auto expected = m_parser.append({body.data(), tmp_sum});
				if( not expected )
				{
					close_connection();
					return io_unexpected(expected.error());
				}
				else if( *expected )
					break;
			}
		}
		if( sum == 0 )
		{
			finish_connection();
			return io_unexpected (
				make_error_code(errc::eof)
			);
		}
		finish_connection();
		return sum;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_read(std::shared_ptr<impl> self, mutable_buffer buf)
	{
		size_t sum = 0;

		if( self->m_first_error )
		{
			co_return std::tuple<error_code,size_t> {
				self->m_first_error, sum
			};
		}
		if( self->m_parser.stage() == parser_t::stage_t::finished )
		{
			self->finish_connection();
			co_return std::tuple {
				make_error_code(errc::eof), sum
			};
		}
		if( self->m_parser.stage() == parser_t::stage_t::header )
		{
			auto [error, reply_status] = co_await co_wait(self);
			ignore_unused(reply_status);
			if( error )
				co_return std::tuple<error_code,size_t>{error, sum};
		}
		if( self->m_parser.stage() == parser_t::stage_t::finished )
		{
			self->finish_connection();
			co_return std::tuple {
				make_error_code(errc::eof), sum
			};
		}
		if( not self->has_connection() )
		{
			co_return std::tuple {
				make_system_error_code(std::errc::not_connected), sum
			};
		}
		auto &conn = self->connection();
		if( not conn.is_open() )
		{
			self->close_connection();
			co_return std::tuple {
				make_system_error_code(std::errc::not_connected), sum
			};
		}
		if( buf.size() == 0 )
			co_return std::tuple{error_code{}, sum};

		auto options = conn.options();
		if( not options )
		{
			self->close_connection();
			co_return std::tuple<error_code,size_t>{options.error(), sum};
		}
		auto read_size = options->receive_buffer_size;
		if( read_size == 0 )
			read_size = 0xFFFF;

		auto dst_buf = static_cast<char*>(buf.data());
		for(;;)
		{
			sum += self->m_parser.read_partial_body (
				{dst_buf + sum, buf.size() - sum}
			);
			if( sum == buf.size() or
				self->m_parser.stage() == parser_t::stage_t::finished )
				break;

			if( auto direct_size = self->m_parser.prepare_direct_body_read(buf.size() - sum) )
			{
				auto [error, bytes] = co_await conn.read (
					{dst_buf + sum, direct_size}, asio::as_tuple(deferred)
				);
				if( error )
				{
					self->close_connection();
					co_return std::tuple<error_code,size_t> {
						sum > 0 ? error_code{} : error, sum
					};
				}
				if( not self->m_parser.commit_direct_body_read(bytes) )
				{
					self->close_connection();
					co_return std::tuple<error_code,size_t> {
						make_system_error_code(std::errc::protocol_error), 0
					};
				}
				sum += bytes;
				continue;
			}
			std::string body(read_size, '\0');
			for(;;)
			{
				auto [error, bytes] = co_await conn.read (
					{body.data(), body.size()}, asio::as_tuple(deferred)
				);
				if( error )
				{
					self->close_connection();
					if( error == errc::eof and self->m_parser.finish_eof() )
						break;

					co_return std::tuple<error_code,size_t> {
						sum > 0 ? error_code{} : error, sum
					};
				}
				auto expected = self->m_parser.append (
					{body.data(), bytes}
				);
				if( not expected )
				{
					self->close_connection();
					co_return std::tuple<error_code,size_t> {
						expected.error(), 0
					};
				}
				if( *expected )
					break;
			}
		}
		if( sum == 0 )
		{
			self->finish_connection();
			co_return std::tuple<error_code,size_t> {
				make_error_code(errc::eof), 0
			};
		}
		self->finish_connection();
		co_return std::tuple{error_code{}, sum};
	}

	template <typename Token>
	[[nodiscard]] auto async_read(const mutable_buffer &output, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), output]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_read(std::move(self), output),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] sys_expected<byte_range_chunk> read_range_body() noexcept
	{
		if( m_first_error )
			return sys_unexpected(m_first_error);

		if( m_parser.stage() == stage::header )
		{
			if( auto expected = wait(); not expected )
				return sys_unexpected(expected.error());
		}
		auto chunk = m_parser.take_range_body(128 * 1024);
		while( not chunk and m_parser.stage() != stage::finished and has_connection() )
		{
			auto &conn = connection();
			if( not conn.is_open() )
			{
				close_connection();
				return sys_unexpected(make_system_error_code(std::errc::not_connected));
			}
			char buf[128 * 1024] {};
			error_code error {};

			auto size = conn.read(buffer(buf), error);
			if( error )
			{
				close_connection();
				return sys_unexpected(error);
			}
			auto expected = m_parser.append({buf, size});
			if( not expected )
			{
				close_connection();
				return sys_unexpected(expected.error());
			}
			chunk = m_parser.take_range_body(128 * 1024);
		}
		if( chunk )
		{
			finish_connection();
			return std::move(*chunk);
		}
		if( m_parser.stage() == stage::finished )
		{
			finish_connection();
			return sys_unexpected(make_error_code(errc::eof));
		}
		return sys_unexpected(make_system_error_code(std::errc::not_connected));
	}

	using range_value_t = byte_range_chunk;

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,range_value_t>,executor_t>
	co_read_range_body(std::shared_ptr<impl> self)
	{
		if( self->m_first_error )
		{
			co_return std::tuple<error_code,range_value_t> {
				self->m_first_error, {}
			};
		}
		if( self->m_parser.stage() == stage::header )
		{
			auto [error, reply_status] = co_await co_wait(self);
			ignore_unused(reply_status);
			if( error )
				co_return std::tuple<error_code,range_value_t>{error, {}};
		}
		for(;;)
		{
			if( auto chunk = self->m_parser.take_range_body(128 * 1024) )
			{
				self->finish_connection();
				co_return std::tuple<error_code,range_value_t> {
					error_code{}, std::move(*chunk)
				};
			}
			if( self->m_parser.stage() == stage::finished )
			{
				self->finish_connection();
				co_return std::tuple<error_code,range_value_t> {
					make_error_code(errc::eof), {}
				};
			}
			if( not self->has_connection() )
			{
				co_return std::tuple<error_code,range_value_t> {
					make_system_error_code(std::errc::not_connected), {}
				};
			}
			auto &conn = self->connection();
			if( not conn.is_open() )
			{
				self->close_connection();
				co_return std::tuple<error_code,range_value_t> {
					make_system_error_code(std::errc::not_connected), {}
				};
			}
			char buf[128 * 1024] {};
			auto [error, bytes] = co_await conn.read (
				buffer(buf), asio::as_tuple(deferred)
			);
			if( error )
			{
				self->close_connection();
				co_return std::tuple<error_code,range_value_t>{error, {}};
			}
			auto expected = self->m_parser.append({buf, bytes});
			if( not expected )
			{
				self->close_connection();
				co_return std::tuple<error_code,range_value_t> {
					expected.error(), {}
				};
			}
		}
		co_return std::tuple<error_code,range_value_t>{};
	}

	template <typename Token>
	[[nodiscard]] auto async_read_range_body(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,range_value_t)>(
		[exec, self = this->shared_from_this()]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_read_range_body(std::move(self)),
				riwo::detail::co_spawn_io_handler<range_value_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] sys_expected<std::vector<std::byte>> read_all() noexcept
	{
		std::vector<std::byte> sum {};
		for(;;)
		{
			constexpr size_t default_buf_size = 64 * 1024;
			auto offset = sum.size();

			auto buf_size = grow_read_all_buffer(sum, default_buf_size);
			if( not buf_size )
				return sys_unexpected(buf_size.error());

			auto expected = read({sum.data() + offset, *buf_size});
			if( expected )
			{
				sum.resize(offset + *expected);
				if( m_parser.stage() == stage::finished )
					break;
				continue;
			}
			sum.resize(offset);
			if( expected.error() == errc::eof )
				break;

			return sys_unexpected(expected.error());
		}
		return std::move(sum);
	}

	using all_buffer_t = std::vector<std::byte>;

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,all_buffer_t>,executor_t>
	co_read_all(std::shared_ptr<impl> self)
	{
		all_buffer_t sum {};
		for(;;)
		{
			constexpr size_t default_buf_size = 64 * 1024;
			auto offset = sum.size();

			auto buf_size = self->grow_read_all_buffer (
				sum, default_buf_size
			);
			if( not buf_size )
			{
				co_return std::tuple<error_code,all_buffer_t> {
					buf_size.error(), {}
				};
			}
			auto [error, bytes] = co_await co_read (
				self, mutable_buffer{sum.data() + offset, *buf_size}
			);
			if( not error )
			{
				sum.resize(offset + bytes);
				if( self->m_parser.stage() == stage::finished )
					break;
				continue;
			}
			sum.resize(offset);
			if( error == errc::eof )
				break;

			co_return std::tuple<error_code,all_buffer_t>{error, {}};
		}
		co_return std::tuple {error_code{}, std::move(sum)};
	}

	template <typename Token>
	[[nodiscard]] auto async_read_all(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,all_buffer_t)>(
		[exec, self = this->shared_from_this()]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_read_all(std::move(self)),
				riwo::detail::co_spawn_io_handler<all_buffer_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

	template <core_concepts::buffer Buffer>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,Buffer>,executor_t>
	co_read_buffer(std::shared_ptr<impl> self)
	{
		if constexpr( is_array_buffer_v<Buffer> )
		{
			Buffer result {};
			auto [error, bytes] = co_await co_read(self, buffer(result));
			ignore_unused(bytes);

			co_return std::tuple<error_code,Buffer> {
				error, std::move(result)
			};
		}
		else
		{
			auto read_result = co_await co_read_all(self);
			auto &[error, source] = read_result;

			if( error )
				co_return std::tuple<error_code,Buffer>{error, {}};

			Buffer result {};
			try {
				result = copy_buffer_data<Buffer>(
					std::move(source)
				);
			}
			catch(...)
			{
				error = exception_error (
					std::current_exception()
				);
			}
			co_return std::tuple<error_code,Buffer> {
				error, std::move(result)
			};
		}
		co_return std::tuple<error_code,Buffer>{};
	}

	template <core_concepts::buffer Buffer, typename Token>
	[[nodiscard]] auto async_read_buffer(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,Buffer)>(
		[exec, self = this->shared_from_this()]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_read_buffer<Buffer>(std::move(self)),
				riwo::detail::co_spawn_io_handler<Buffer,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] io_expected save_file(auto &&opt, auto &&progress) noexcept
	{
		io_expected expected {};
		if( m_parser.stage() == stage::header )
		{
			if( auto status = wait(); not status )
				return expected.despair(status.error());
		}
		if( m_parser.status() == status::range_not_satisfiable )
		{
			close_connection();
			return expected.despair (
				std::make_error_code(std::errc::result_out_of_range)
			);
		}
		auto token = make_file_opt_token(std::forward<decltype(opt)>(opt),
			m_parser.status() == status::partial_content
		);
		if( not token )
		{
			close_connection();
			return expected.despair(token.error());
		}
		size_t sum = 0, total = 0;
		if( auto complete = m_parser.complete_length() )
			total = *complete;

		else if( auto length = m_parser.header(header::content_length);
				 not m_parser.is_range_response() and length )
			total = *length->get<size_t>().or_else(0);

		for(;;)
		{
			auto chunk = read_range_body();
			if( not chunk )
			{
				expected.despair(chunk.error());
				break;
			}
			token->stream->seekp(chunk->offset, std::ios::beg);
			token->stream->write(chunk->data.data(), chunk->data.size());

			if( not *token->stream )
			{
				expected.despair(std::make_error_code(std::errc::io_error));
				break;
			}
			sum += chunk->data.size();
			if( auto complete = m_parser.complete_length() )
				total = *complete;

			if( auto error = invoke_progress(progress, sum, total) )
			{
				expected.despair(error);
				break;
			}
		}
		token->stream->close();
		if( not expected and expected.error() != errc::eof )
		{
			close_connection();
			return expected;
		}
		return sum;
	}

	template <typename Progress>
	[[nodiscard]] static asio::awaitable<error_code,executor_t>
	co_invoke_progress(executor_t exec, Progress &progress, size_t sum, size_t total)
	{
		using result_t = decltype(progress(sum, total));
		if constexpr( is_awaitable_v<result_t> )
		{
			try {
				using progress_value_t = result_t::value_type;
				if constexpr( std::same_as<progress_value_t,bool> )
				{
					auto [exception, keep_going] = co_await asio::co_spawn (
						exec, progress(sum, total), asio::as_tuple(deferred)
					);
					if( exception )
						co_return exception_error(exception);

					co_return keep_going ?
						error_code{} : make_error_code(errc::operation_aborted);
				}
				else
				{
					auto [exception] = co_await asio::co_spawn (
						exec, progress(sum, total), asio::as_tuple(deferred)
					);
					co_return exception_error(exception);
				}
			}
			catch(...) {
				co_return exception_error(std::current_exception());
			}
		}
		else
		{
			error_code error {};
			try {
				if constexpr( std::same_as<result_t,bool> )
				{
					if( not progress(sum, total) )
						error = make_error_code(errc::operation_aborted);
				}
				else
					progress(sum, total);
			}
			catch(...) {
				error = exception_error(std::current_exception());
			}
			co_return error;
		}
		co_return error_code{};
	}

	template <typename AsyncOpt, typename AsyncProgress>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_save_file(std::shared_ptr<impl> self, AsyncOpt opt, AsyncProgress progress)
	{
		if( self->m_parser.stage() == stage::header )
		{
			auto [error, reply_status] = co_await co_wait(self);
			ignore_unused(reply_status);
			if( error )
				co_return std::tuple<error_code,size_t>{error, 0};
		}
		if( self->m_parser.status() == status::range_not_satisfiable )
		{
			self->close_connection();
			co_return std::tuple<error_code,size_t>{
				make_system_error_code(std::errc::result_out_of_range), 0
			};
		}
		auto file_token = self->make_file_opt_token (
			unwrap_async_argument(opt), self->m_parser.status() == status::partial_content
		);
		if( not file_token )
		{
			self->close_connection();
			co_return std::tuple<error_code,size_t> {
				file_token.error(), 0
			};
		}
		size_t sum = 0, total = 0;
		if( auto complete = self->m_parser.complete_length() )
			total = *complete;

		else if( auto length = self->m_parser.header(header::content_length);
				 not self->m_parser.is_range_response() and length )
			total = *length->template get<size_t>().or_else(0);

		auto &progress_ref = unwrap_async_argument(progress);
		for(;;)
		{
			auto [error, chunk] = co_await co_read_range_body(self);
			if( error )
			{
				file_token->stream->close();
				if( error == errc::eof )
				{
					co_return std::tuple {
						error_code{}, sum
					};
				}
				self->close_connection();
				co_return std::tuple<error_code,size_t>{error, 0};
			}
			file_token->stream->seekp(chunk.offset, std::ios::beg);
			file_token->stream->write(chunk.data.data(), chunk.data.size());

			if( not *file_token->stream )
			{
				file_token->stream->close();
				self->close_connection();

				co_return std::tuple<error_code,size_t> {
					make_system_error_code(std::errc::io_error), 0
				};
			}
			sum += chunk.data.size();
			if( auto complete = self->m_parser.complete_length() )
				total = *complete;

			auto progress_error = co_await co_invoke_progress (
				self->m_exec, progress_ref, sum, total
			);
			if( progress_error )
			{
				file_token->stream->close();
				self->close_connection();
				co_return std::tuple<error_code,size_t>{progress_error, 0};
			}
		}
		co_return std::tuple<error_code,size_t>{};
	}

	template <typename AsyncOpt, typename AsyncProgress, typename Token>
	[[nodiscard]] auto async_save_file
	(AsyncOpt async_opt, AsyncProgress async_progress, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using progress_t = std::remove_cvref_t<AsyncProgress>;
		using token_t = std::remove_cvref_t<Token>;

		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,size_t)>([exec,
			self = this->shared_from_this(), opt = opt_t(std::move(async_opt)),
			progress = progress_t(std::move(async_progress))
		]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_save_file(std::move(self), std::move(opt),
				std::move(progress)),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

private:
	[[nodiscard]] sys_expected<size_t>
	grow_read_all_buffer(std::vector<std::byte> &sum, size_t default_size) const noexcept
	{
		auto offset = sum.size();
		size_t direct_remaining = 0;

		auto read_size = m_parser.partial_body_size();
		if( read_size == 0 )
		{
			direct_remaining = m_parser.prepare_direct_body_read (
				std::numeric_limits<size_t>::max()
			);
			read_size = direct_remaining == 0 ? default_size :
				std::min(default_size, direct_remaining);
		}
		if( read_size > sum.max_size() - offset or
			(direct_remaining != 0 and direct_remaining > sum.max_size() - offset) )
		{
			return sys_unexpected (
				make_system_error_code(std::errc::value_too_large)
			);
		}
		try {
			if( constexpr size_t max_preallocated_body_size = 8 * 1024 * 1024;
				direct_remaining != 0 and direct_remaining <= max_preallocated_body_size and
				sum.capacity() < offset + direct_remaining )
				sum.reserve(offset + direct_remaining);
			sum.resize(offset + read_size);
		}
		catch(const std::length_error&)
		{
			return sys_unexpected (
				make_system_error_code(std::errc::value_too_large)
			);
		}
		catch(const std::bad_alloc&)
		{
			return sys_unexpected (
				make_system_error_code(std::errc::not_enough_memory)
			);
		}
		return read_size;
	}

	[[nodiscard]] error_code invoke_progress(auto &progress, size_t sum, size_t total) noexcept
	{
		try {
			using pro_ret_t = decltype(progress(0, 0));
			if constexpr( std::is_same_v<pro_ret_t, bool> )
			{
				if( progress(sum, total) )
					return {};
				return make_error_code(errc::operation_aborted);
			}
			else
				progress(sum, total);
		}
		catch(...) {
			return exception_error(std::current_exception());
		}
		return {};
	}

	template <typename Opt>
	auto make_file_opt_token(Opt &&opt, bool preserve) noexcept
	{
		using opt_t = std::remove_cvref_t<Opt>;
		if constexpr( is_any_string_v<opt_t> or std::same_as<opt_t,std::filesystem::path> or
			is_fstream_v<opt_t,char> or is_ofstream_v<opt_t,char> )
		{
			auto token = http::make_file_opt_token(std::forward<Opt>(opt));
			using token_t = decltype(token);

			auto mode = std::ios::out | std::ios::binary | std::ios::trunc;
			if constexpr( requires { token.file_name; } )
			{
				if( preserve and std::filesystem::exists(token.file_name) )
					mode = std::ios::in | std::ios::out | std::ios::binary;
			}
			auto expected = token.init(mode);
			if( expected )
				return sys_expected<token_t>(std::move(token));

			return sys_expected<token_t>(sys_unexpected(expected.error()));
		}
		else
		{
			if( opt.stream->is_open() )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			auto mode = preserve ?
				std::ios::in | std::ios::out | std::ios::binary :
				std::ios::out | std::ios::binary | std::ios::trunc;

			auto expected = opt.init(mode);
			if( expected )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			return sys_expected<opt_t>(sys_unexpected(expected.error()));
		}
	}

	[[nodiscard]] bool has_connection() const noexcept {
		return m_lease and m_lease->is_valid();
	}

	void close_connection() noexcept
	{
		if( not has_connection() )
			return ;

		if( auto connection = m_lease->take() )
			ignore_unused(connection->close());
	}

	void finish_connection() noexcept
	{
		if( not has_connection() or
			m_parser.stage() != parser_t::stage_t::finished or
			m_parser.is_informational() or m_parser.is_upgrade() )
			return ;

		auto pending = m_parser.take_pending_data();
		if( not m_parser.keep_alive() or not pending.empty() )
		{
			close_connection();
			return ;
		}
		try {
			m_lease->release();
		}
		catch(...) {
			close_connection();
		}
	}

	[[nodiscard]] connection_t &connection() noexcept {
		return m_lease->get();
	}

public:
	lease_ptr m_lease {};
	executor_t m_exec {};

	error_code m_first_error {};
	parser_t m_parser {};

	std::shared_ptr<cookie_jar> m_cookie_jar {};
	url m_origin {};

	bool m_cookies_captured = false;
};

namespace detail
{

struct RIWO_HTTP_TAPI reply_access
{
	template <core_concepts::exec Exec>
	[[nodiscard]] static auto wait(basic_reply<Exec> &reply) {
		return basic_reply<Exec>::impl::co_wait(reply.m_impl);
	}
};

} //namespace detail

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply(lease_ptr lease) :
	const_headers<basic_reply>(nullptr),
	const_cookies<cookie,basic_reply>(nullptr),
	m_impl(std::make_shared<impl>(std::move(lease)))
{
	this->m_headers = &m_impl->m_parser.headers();
	this->m_cookies = &m_impl->m_parser.cookies();
}

template <core_concepts::exec Exec>
basic_reply<Exec>::basic_reply(lease_ptr lease, parser_t &&parser) :
	const_headers<basic_reply>(nullptr),
	const_cookies<cookie,basic_reply>(nullptr),
	m_impl(std::make_shared<impl>(std::move(lease), std::move(parser)))
{
	this->m_headers = &m_impl->m_parser.headers();
	this->m_cookies = &m_impl->m_parser.cookies();
}

template <core_concepts::exec Exec>
basic_reply<Exec>::~basic_reply() = default;

template <core_concepts::exec Exec>
template <typename Token>
auto basic_reply<Exec>::wait(Token &&token)
	requires task_token_v<Token,status_enum>
{
	if constexpr( is_error_code_token_v<Token> )
		return expected_value_or_error(m_impl->wait(), token);

	else if constexpr( is_sync_opt_token_v<Token> )
		return expected_value_or_throw(m_impl->wait());
	else
	{
		return initiate_io<status_enum>(get_executor(),
		[impl = m_impl]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_wait (
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_reply<Exec>::read(const mutable_buffer &buf, Token &&token)
	requires task_token_v<Token,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
		return expected_value_or_error(m_impl->read(buf), token);

	else if constexpr( is_sync_opt_token_v<Token> )
		return expected_value_or_throw(m_impl->read(buf));
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, buf]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_read(buf,
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}
template <core_concepts::exec Exec>
template <core_concepts::buffer Buffer, typename Token>
auto basic_reply<Exec>::read(Token &&token)
	requires task_token_v<Token,Buffer>
{
	if constexpr( is_array_buffer_v<Buffer> )
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			Buffer result {};
			ignore_unused(expected_value_or_error (
				m_impl->read(buffer(result)), token
			));
			return result;
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			Buffer result {};
			ignore_unused(expected_value_or_throw (
				m_impl->read(buffer(result))
			));
			return result;
		}
		else
		{
			return initiate_io<Buffer>(get_executor(),
			[impl = m_impl]<typename T0>(T0 &&completion_token) mutable
			{
				return impl->template async_read_buffer<Buffer>(
					std::forward<T0>(completion_token)
				);
			},
			std::forward<Token>(token));
		}
	}
	else if constexpr( is_error_code_token_v<Token> )
	{
		auto source = expected_value_or_error(m_impl->read_all(), token);
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		auto source = expected_value_or_throw(m_impl->read_all());
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else
	{
		return initiate_io<Buffer>(get_executor(),
		[impl = m_impl]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->template async_read_buffer<Buffer>(
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_reply<Exec>::read(Token &&token)
	requires task_token_v<Token,std::vector<std::byte>>
{
	return read<std::vector<std::byte>>(std::forward<Token>(token));
}
template <core_concepts::exec Exec>
template <typename T, typename Token>
auto basic_reply<Exec>::save_file(T &&opt, Token &&token)
	requires file_task_token<T,Token>
{
	return save_file(std::forward<T>(opt),
		[](size_t,size_t){}, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
template <typename T, typename Progress, typename Token>
auto basic_reply<Exec>::save_file(T &&opt, Progress &&progress, Token &&token)
	requires file_task_token<T,Token> and concepts::progress_callback<Progress,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error(m_impl->save_file (
			std::forward<T>(opt), std::forward<Progress>(progress)
		), token);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw(m_impl->save_file (
			std::forward<T>(opt), std::forward<Progress>(progress)
		));
	}
	else
	{
		return initiate_io<size_t>(get_executor(), [impl = m_impl,
			async_opt = capture_async_argument(std::forward<T>(opt)),
			async_progress = capture_async_argument (
				std::forward<Progress>(progress)
			)
		]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_save_file (
				std::move(async_opt), std::move(async_progress),
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
version_enum basic_reply<Exec>::version() const noexcept
{
	return m_impl->m_parser.version();
}

template <core_concepts::exec Exec>
status_enum basic_reply<Exec>::status() const noexcept
{
	return m_impl->m_parser.status();
}

template <core_concepts::exec Exec>
bool basic_reply<Exec>::valid() const noexcept
{
	return m_impl->m_parser.stage() != stage::header;
}

template <core_concepts::exec Exec>
error_code basic_reply<Exec>::first_error() const noexcept
{
	return m_impl->m_first_error;
}

template <core_concepts::exec Exec>
bool basic_reply<Exec>::content_decoded() const noexcept
{
	return m_impl->m_parser.content_decoded();
}

template <core_concepts::exec Exec>
bool basic_reply<Exec>::is_chunked() const noexcept
{
	return m_impl->m_parser.is_chunked();
}

template <core_concepts::exec Exec>
bool basic_reply<Exec>::is_eof() const noexcept
{
	return m_impl->m_parser.stage() == stage::finished;
}

template <core_concepts::exec Exec>
bool basic_reply<Exec>::is_upgrade() const noexcept
{
	return m_impl->m_parser.is_upgrade();
}

template <core_concepts::exec Exec>
std::string basic_reply<Exec>::take_pending_data()
{
	return m_impl->m_parser.take_pending_data();
}

template <core_concepts::exec Exec>
const basic_reply<Exec>::lease_t &basic_reply<Exec>::lease() const noexcept
{
	return *m_impl->m_lease;
}

template <core_concepts::exec Exec>
basic_reply<Exec>::lease_t &basic_reply<Exec>::lease() noexcept
{
	return *m_impl->m_lease;
}

template <core_concepts::exec Exec>
const basic_reply<Exec>::parser_t &basic_reply<Exec>::parser() const noexcept
{
	return m_impl->m_parser;
}

template <core_concepts::exec Exec>
basic_reply<Exec>::parser_t &basic_reply<Exec>::parser() noexcept
{
	return m_impl->m_parser;
}

template <core_concepts::exec Exec>
basic_reply<Exec>::executor_t basic_reply<Exec>::get_executor() noexcept
{
	return m_impl->m_exec;
}

template <core_concepts::exec Exec>
basic_reply<Exec> &basic_reply<Exec>::bind_cookie_jar
(std::shared_ptr<cookie_jar> jar, url origin)
{
	m_impl->m_cookie_jar = std::move(jar);
	m_impl->m_origin = std::move(origin);
	m_impl->capture_cookies();
	return *this;
}

template <core_concepts::exec Exec>
basic_reply<Exec> &basic_reply<Exec>::cancel() noexcept
{
	if( m_impl->m_lease and m_impl->m_lease->is_valid() )
	{
		ignore_unused(m_impl->m_lease->get().cancel());
		if( auto conn = m_impl->m_lease->take() )
			ignore_unused(conn->close());
	}
	return *this;
}

} //namespace riwo::http

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_HTTP_CLIENT_DETAIL_REPLY_H
