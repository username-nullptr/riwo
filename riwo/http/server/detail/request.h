// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_REQUEST_H
#define RIWO_HTTP_SERVER_DETAIL_REQUEST_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_request<Exec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY(impl)

public:
	explicit impl(connection_ptr conn, std::filesystem::path resource_root) :
		m_connection(std::move(conn)),
		m_resource_root(std::move(resource_root)) {}

	impl(connection_ptr conn, parser_t &&parser, std::filesystem::path resource_root) :
		m_connection(std::move(conn)),
		m_parser(std::move(parser)),
		m_resource_root(std::move(resource_root)) {}

public:
	void wait(error_code &error) noexcept
	{
		error.clear();
		if( m_parser.stage() != parser_t::stage_t::header )
			return ;

		auto &conn = *m_connection;
		if( not conn.is_open() )
		{
			error = make_system_error_code(std::errc::not_connected);
			return ;
		}
		using namespace riwo::operators;

		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size] {};
		for(;;)
		{
			auto sum = conn.read(buffer(buf, buf_size), error);
			if( error )
			{
				ignore_unused(conn.close());
				return ;
			}
			auto expected = m_parser.append({buf, sum});
			if( not expected )
			{
				ignore_unused(conn.close());
				error = expected.error();
				return ;
			}
			else if( *expected )
				break;
		}
	}

	[[nodiscard]] static asio::awaitable<error_code,Exec>
	co_wait(std::shared_ptr<impl> self)
	{
		if( self->m_parser.stage() != parser_t::stage_t::header )
			co_return error_code{};

		auto &conn = *self->m_connection;
		if( not conn.is_open() )
			co_return make_system_error_code(std::errc::not_connected);

		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size];
		for(;;)
		{
			auto [error, sum] = co_await conn.read (
				buffer(buf, buf_size), asio::as_tuple(deferred)
			);
			if( error )
			{
				ignore_unused(conn.close());
				co_return error;
			}
			auto expected = self->m_parser.append({buf, sum});
			if( not expected )
			{
				ignore_unused(conn.close());
				co_return expected.error();
			}
			if( *expected )
				co_return error_code{};
		}
		co_return error_code{};
	}

	template <typename Token>
	[[nodiscard]] auto async_wait(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code)>(
		[exec, self = this->shared_from_this()]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_wait(std::move(self)),
				riwo::detail::co_spawn_error_handler<Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] bool expects_continue() const noexcept
	{
		if( m_continue_sent or m_parser.version() < version::v11 )
			return false;

		auto it = m_parser.headers().find(header::expect);
		return it != m_parser.headers().end() and
			strtls::to_lower(strtls::trimmed(it->second.to_string())) == "100-continue";
	}

	void send_continue(error_code &error) noexcept
	{
		if( not expects_continue() )
			return ;

		ignore_unused(m_connection->write (
			"HTTP/1.1 100 Continue\r\n\r\n", error
		));
		if( not error )
			m_continue_sent = true;
	}

	[[nodiscard]] size_t read(const mutable_buffer &buf, error_code &error) noexcept
	{
		error.clear();
		const size_t buf_size = buf.size();

		if( buf_size == 0 )
			return 0;

		if( m_parser.stage() == stage::header )
		{
			wait(error);
			if( error )
				return 0;
		}
		if( m_parser.stage() != stage::body and m_parser.partial_body_size() == 0 )
		{
			error = make_error_code(errc::eof);
			return 0;
		}
		send_continue(error);
		if( error )
			return 0;

		auto options = m_connection->options();
		if( not options )
		{
			error = options.error();
			return 0;
		}
		auto receive_buffer_size = options->receive_buffer_size;
		if( receive_buffer_size == 0 )
			receive_buffer_size = 0xFFFF;

		auto dst_buf = static_cast<char*>(buf.data());
		size_t sum = 0;
		do {
			sum += m_parser.read_partial_body (
				{dst_buf + sum, buf_size - sum}
			);
			if( sum == buf_size or m_parser.stage() == stage::finished )
				break;

			if( auto read_size = m_parser.prepare_direct_body_read(buf_size - sum) )
			{
				auto bytes = m_connection->read({dst_buf + sum, read_size}, error);
				if( error )
					return sum;

				if( not m_parser.commit_direct_body_read(bytes) )
				{
					error = make_system_error_code(std::errc::protocol_error);
					return sum;
				}
				sum += bytes;
				continue;
			}
			std::string body(receive_buffer_size,'\0');
			for(;;)
			{
				auto tmp_sum = m_connection->read (
					{body.data(), body.size()}, error
				);
				if( error )
					return sum;

				auto expected = m_parser.append({body.data(), tmp_sum});
				if( not expected )
				{
					error = expected.error();
					return sum;
				}
				else if( *expected )
					break;
			}
		}
		while(true);
		return sum;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_read(std::shared_ptr<impl> self, mutable_buffer buf)
	{
		size_t sum = 0;

		if( buf.size() == 0 )
			co_return std::tuple{error_code{}, sum};

		if( self->m_parser.stage() == stage::header )
		{
			if( auto error = co_await co_wait(self) )
				co_return std::tuple<error_code,size_t>{error, sum};
		}
		if( self->m_parser.stage() != stage::body and self->m_parser.partial_body_size() == 0 )
			co_return std::tuple{make_error_code(errc::eof), sum};

		auto options = self->m_connection->options();
		if( not options )
			co_return std::tuple{options.error(), sum};

		auto receive_buffer_size = options->receive_buffer_size;
		if( receive_buffer_size == 0 )
			receive_buffer_size = 0xFFFF;

		if( self->expects_continue() )
		{
			auto [error, bytes] = co_await self->m_connection->write (
				"HTTP/1.1 100 Continue\r\n\r\n", asio::as_tuple(deferred)
			);
			ignore_unused(bytes);
			if( error )
				co_return std::tuple<error_code,size_t>{error, sum};
			self->m_continue_sent = true;
		}
		auto dst_buf = static_cast<char*>(buf.data());
		for(;;)
		{
			sum += self->m_parser.read_partial_body (
				{dst_buf + sum, buf.size() - sum}
			);
			if( sum == buf.size() or self->m_parser.stage() == stage::finished )
				co_return std::tuple{error_code{}, sum};

			if( auto read_size = self->m_parser.prepare_direct_body_read(buf.size() - sum) )
			{
				auto [error, bytes] = co_await self->m_connection->read (
					{dst_buf + sum, read_size}, asio::as_tuple(deferred)
				);
				if( error )
					co_return std::tuple<error_code,size_t>{error, sum};

				if( not self->m_parser.commit_direct_body_read(bytes) )
				{
					co_return std::tuple {
						make_system_error_code(std::errc::protocol_error), sum
					};
				}
				sum += bytes;
				continue;
			}
			std::string body(receive_buffer_size, '\0');
			for(;;)
			{
				auto [error, bytes] = co_await self->m_connection->read (
					{body.data(), body.size()}, asio::as_tuple(deferred)
				);
				if( error )
					co_return std::tuple<error_code,size_t>{error, sum};

				auto expected = self->m_parser.append (
					{body.data(), bytes}
				);
				if( not expected )
				{
					co_return std::tuple<error_code,size_t> {
						expected.error(), sum
					};
				}
				if( *expected )
					break;
			}
		}
		co_return std::tuple<error_code,size_t>{};
	}

	template <typename Token>
	[[nodiscard]] auto async_read(const mutable_buffer &output, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), output]
		<typename Handler>(Handler completion_handler) mutable
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
	[[nodiscard]] std::vector<std::byte> read_all(error_code &error) noexcept
	{
		error.clear();
		std::vector<std::byte> sum {};

		if( m_parser.stage() == stage::header )
		{
			wait(error);
			if( error )
				return sum;
		}
		if( m_parser.stage() != stage::body and m_parser.partial_body_size() == 0 )
			return sum;

		auto options = m_connection->options();
		if( not options )
		{
			error = options.error();
			return sum;
		}
		auto buf_size = options->receive_buffer_size;
		if( buf_size == 0 )
			buf_size = 64 * 1024;
		do {
			auto offset = sum.size();
			auto read_size = grow_read_all_buffer(sum, buf_size, error);
			if( error )
				return sum;

			auto bytes = read({sum.data() + offset, read_size}, error);
			if( error )
			{
				sum.resize(offset);
				return sum;
			}
			sum.resize(offset + bytes);
		}
		while( m_parser.stage() == stage::body or m_parser.partial_body_size() > 0 );
		return sum;
	}

	using all_buffer_t = std::vector<std::byte>;

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,all_buffer_t>,Exec>
	co_read_all(std::shared_ptr<impl> self)
	{
		all_buffer_t sum {};

		if( self->m_parser.stage() == stage::header )
		{
			if( auto error = co_await co_wait(self) )
			{
				co_return std::tuple<error_code,all_buffer_t> {
					error, {}
				};
			}
		}
		if( self->m_parser.stage() != stage::body and self->m_parser.partial_body_size() == 0 )
		{
			co_return std::tuple {
				error_code{}, std::move(sum)
			};
		}
		auto options = self->m_connection->options();
		if( not options )
		{
			co_return std::tuple<error_code,all_buffer_t> {
				options.error(), {}
			};
		}
		auto buf_size = options->receive_buffer_size;
		if( buf_size == 0 )
			buf_size = 64 * 1024;
		do {
			auto offset = sum.size();
			error_code error {};

			auto read_size = self->grow_read_all_buffer (
				sum, buf_size, error
			);
			if( error )
			{
				co_return std::tuple<error_code,all_buffer_t> {
					error, {}
				};
			}
			auto [read_error, bytes] = co_await co_read (
				self, mutable_buffer{sum.data() + offset, read_size}
			);
			if( read_error )
			{
				co_return std::tuple<error_code,all_buffer_t> {
					read_error, {}
				};
			}
			sum.resize(offset + bytes);
		}
		while (
			self->m_parser.stage() == stage::body or
			self->m_parser.partial_body_size() > 0
		);
		co_return std::tuple {error_code{}, std::move(sum)};
	}

	template <typename Token>
	[[nodiscard]] auto async_read_all(Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

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
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,Buffer>,Exec>
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
		auto exec = m_connection->get_executor();

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
	[[nodiscard]] std::filesystem::path
	resource_file_name(std::filesystem::path file_name) const
	{
		if( file_name.empty() or m_resource_root.empty() or app::is_absolute_path(file_name) )
			return file_name;
		return m_resource_root / file_name;
	}

	template <typename Opt>
	auto make_file_opt_token(Opt &&opt) noexcept
	{
		using opt_t = std::remove_cvref_t<Opt>;
		if constexpr( is_any_string_v<opt_t> or std::same_as<opt_t,std::filesystem::path> or
			is_fstream_v<opt_t,char> or is_ofstream_v<opt_t,char> )
		{
			auto token = http::make_file_opt_token(std::forward<Opt>(opt));
			using token_t = decltype(token);

			if constexpr( requires { token.file_name; } )
				token.file_name = resource_file_name(std::move(token.file_name));

			auto expected = token.init (
				std::ios::out | std::ios::binary | std::ios::trunc
			);
			if( expected )
				return sys_expected<token_t>(std::move(token));

			return sys_expected<token_t>(sys_unexpected(expected.error()));
		}
		else
		{
			if( opt.stream->is_open() )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			if constexpr( requires { opt.file_name; } )
				opt.file_name = resource_file_name(std::move(opt.file_name));

			auto expected = opt.init (
				std::ios::out | std::ios::binary | std::ios::trunc
			);
			if( expected )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			return sys_expected<opt_t>(sys_unexpected(expected.error()));
		}
	}

public:
	[[nodiscard]] size_t save_file(auto &&opt, error_code &error) noexcept
	{
		error.clear();
		size_t sum = 0;

		auto expected = make_file_opt_token (
			std::forward<decltype(opt)>(opt)
		);
		if( not expected )
		{
			error = expected.error();
			return sum;
		}
		constexpr size_t buf_size = 128 * 1024;

		auto &token = *expected;
		char buffer[buf_size] {0};
		for(;;)
		{
			auto bytes = read({buffer, buf_size}, error);
			if( error )
				break;

			token.stream->write(buffer, static_cast<std::streamsize>(bytes));
			if( not *token.stream )
			{
				error = make_system_error_code(std::errc::io_error);
				break;
			}
			sum += bytes;
		}
		token.stream->close();

		if( error == errc::eof )
			error.clear();

		else if( error )
			return sum;
		return sum;
	}

	template <typename AsyncOpt>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_save_file(std::shared_ptr<impl> self, AsyncOpt opt)
	{
		auto expected = self->make_file_opt_token (
			unwrap_async_argument(opt)
		);
		if( not expected )
		{
			co_return std::tuple<error_code,size_t> {
				expected.error(), 0
			};
		}
		auto &file_token = *expected;
		size_t sum = 0;
		for(;;)
		{
			constexpr size_t buf_size = 128 * 1024;
			char buf[buf_size] {};

			auto [error, bytes] = co_await co_read (
				self, mutable_buffer{buf, buf_size}
			);
			if( error )
			{
				file_token.stream->close();
				if( error == errc::eof )
				{
					co_return std::tuple {
						error_code{}, sum
					};
				}
				co_return std::tuple<error_code,size_t>{error, 0};
			}
			file_token.stream->write(buf, static_cast<std::streamsize>(bytes));
			if( not *file_token.stream )
			{
				file_token.stream->close();
				co_return std::tuple<error_code,size_t> {
					make_system_error_code(std::errc::io_error), 0
				};
			}
			sum += bytes;
		}
		co_return std::tuple<error_code,size_t>{};
	}

	template <typename AsyncOpt, typename Token>
	[[nodiscard]] auto async_save_file(AsyncOpt async_opt, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), opt = opt_t(std::move(async_opt))]
		<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_save_file(std::move(self), std::move(opt)),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

private:
	[[nodiscard]] size_t grow_read_all_buffer
	(std::vector<std::byte> &sum, size_t default_size, error_code &error) const noexcept
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
			error = make_system_error_code(std::errc::value_too_large);
			return 0;
		}
		try
		{
			if(constexpr size_t max_preallocated_body_size = 8 * 1024 * 1024;
				direct_remaining != 0 and direct_remaining <= max_preallocated_body_size and
				sum.capacity() < offset + direct_remaining )
				sum.reserve(offset + direct_remaining);
			sum.resize(offset + read_size);
		}
		catch(const std::length_error&)
		{
			error = make_system_error_code(std::errc::value_too_large);
			return 0;
		}
		catch(const std::bad_alloc&)
		{
			error = make_system_error_code(std::errc::not_enough_memory);
			return 0;
		}
		return read_size;
	}

public:
	connection_ptr m_connection {};
	parser_t m_parser {};

	std::filesystem::path m_resource_root {};
	bool m_continue_sent = false;
};

template <core_concepts::exec Exec>
basic_request<Exec>::basic_request
(connection_ptr connection, std::filesystem::path resource_root) :
	const_headers<basic_request>(nullptr),
	const_cookies<value_t,basic_request>(nullptr),
	const_parameters<basic_request>(nullptr),
	m_impl(std::make_shared<impl>(std::move(connection), std::move(resource_root)))
{
	this->m_headers = &m_impl->m_parser.headers();
	this->m_cookies = &m_impl->m_parser.cookies();
	this->m_parameters = &m_impl->m_parser.parameters();
}

template <core_concepts::exec Exec>
basic_request<Exec>::basic_request
(connection_ptr connection, parser_t &&parser, std::filesystem::path resource_root) :
	const_headers<basic_request>(nullptr),
	const_cookies<value_t,basic_request>(nullptr),
	const_parameters<basic_request>(nullptr),
	m_impl(std::make_shared<impl>(std::move(connection), std::move(parser),
		std::move(resource_root)))
{
	this->m_headers = &m_impl->m_parser.headers();
	this->m_cookies = &m_impl->m_parser.cookies();
	this->m_parameters = &m_impl->m_parser.parameters();
}

template <core_concepts::exec Exec>
basic_request<Exec>::~basic_request() = default;

template <core_concepts::exec Exec>
template <typename Token>
auto basic_request<Exec>::wait(Token &&token)
	requires task_token_v<Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		m_impl->wait(adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->wait(error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request::wait"
			);
		}
	}
	else
	{
		return initiate_io_void(get_executor(),
		[impl = m_impl]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_wait(
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
int32_t basic_request<Exec>::path_match(std::string_view rule)
{
	return m_impl->m_parser.path_match(rule);
}

template <core_concepts::exec Exec>
method_enum basic_request<Exec>::method() const noexcept
{
	return m_impl->m_parser.method();
}

template <core_concepts::exec Exec>
request_target_form basic_request<Exec>::target_form() const noexcept
{
	return m_impl->m_parser.target_form();
}

template <core_concepts::exec Exec>
std::string_view basic_request<Exec>::target() const noexcept
{
	return m_impl->m_parser.target();
}

template <core_concepts::exec Exec>
version_enum basic_request<Exec>::version() const noexcept
{
	return m_impl->m_parser.version();
}

template <core_concepts::exec Exec>
std::string_view basic_request<Exec>::path() const noexcept
{
	return m_impl->m_parser.path();
}

template <core_concepts::exec Exec>
auto basic_request<Exec>::path_arg
(const core_concepts::text_p<char> auto &key) const noexcept -> optional<value_t>
{
	return m_impl->m_parser.path_arg(key);
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::contains_path_arg(const core_concepts::text_p<char> auto &key) const noexcept
{
	auto &args = m_impl->m_parser.path_args();
	return args.find(key) != args.end();
}

template <core_concepts::exec Exec>
auto basic_request<Exec>::path_arg(size_t index) const -> optional<value_t>
{
	return m_impl->m_parser.path_arg(index);
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::contains_path_arg(size_t index) const noexcept
{
	return index < m_impl->m_parser.path_args().size();
}

template <core_concepts::exec Exec>
auto basic_request<Exec>::path_args() const noexcept -> const parameters_t&
{
	return m_impl->m_parser.path_args();
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_request<Exec>::read(const mutable_buffer &buf, Token &&token)
	requires task_token_v<Token,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->read(buf, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto sum = m_impl->read(buf, error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request::read"
			);
		}
		return sum;
	}
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
auto basic_request<Exec>::read(Token &&token)
	requires task_token_v<Token,Buffer>
{
	if constexpr( is_array_buffer_v<Buffer> )
	{
		if constexpr( is_error_code_token_v<Token> )
		{
			Buffer result {};
			auto adapted_error = adapt_error_code(token);
			ignore_unused(m_impl->read(buffer(result), adapted_error.get()));
			return result;
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			Buffer result {};
			error_code error {};
			ignore_unused(m_impl->read(buffer(result), error));
			if( error )
			{
				system_error::loc_throw (
					error, "riwo::http::basic_request::read"
				);
			}
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
		auto adapted_error = adapt_error_code(token);
		auto source = m_impl->read_all(adapted_error.get());
		return copy_buffer_data<Buffer>(std::move(source));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto source = m_impl->read_all(error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request::read"
			);
		}
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
auto basic_request<Exec>::read(Token &&token)
	requires task_token_v<Token,std::vector<std::byte>>
{
	return read<std::vector<std::byte>>(std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename T, typename Token>
auto basic_request<Exec>::save_file(T &&opt, Token &&token)
	requires file_task_token_v<T,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->save_file(std::forward<T>(opt), adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto sum = m_impl->save_file (
			std::forward<T>(opt), error
		);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request::save_file"
			);
		}
		return sum;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl,
		 async_opt = capture_async_argument(std::forward<T>(opt))]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_save_file (
				std::move(async_opt), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::keep_alive() const noexcept
{
	return m_impl->m_parser.keep_alive();
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::support_gzip() const noexcept
{
	return m_impl->m_parser.support_gzip();
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::is_chunked() const noexcept
{
	if( version() < http::version::v11 )
		return false;

	auto value = this->header(http::header::transfer_encoding);
	return value and strtls::to_lower(**value) == "chunked";
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::can_read_body() const noexcept
{
	return m_impl->m_parser.stage() == stage::body;
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::is_eof() const noexcept
{
	return m_impl->m_parser.stage() == stage::finished;
}

template <core_concepts::exec Exec>
bool basic_request<Exec>::is_upgrade() const noexcept
{
	return is_upgrade_request(this->headers());
}

template <core_concepts::exec Exec>
std::string basic_request<Exec>::take_pending_data()
{
	return m_impl->m_parser.take_pending_data();
}

template <core_concepts::exec Exec>
endpoint basic_request<Exec>::remote_endpoint() const
{
	return connection().remote_endpoint();
}

template <core_concepts::exec Exec>
endpoint basic_request<Exec>::local_endpoint() const
{
	return connection().local_endpoint();
}

template <core_concepts::exec Exec>
basic_request<Exec>::executor_t basic_request<Exec>::get_executor() noexcept
{
	return connection().get_executor();
}

template <core_concepts::exec Exec>
basic_request<Exec> &basic_request<Exec>::cancel() noexcept
{
	ignore_unused(connection().cancel());
	return *this;
}

template <core_concepts::exec Exec>
auto basic_request<Exec>::connection() const noexcept -> const connection_t&
{
	return *m_impl->m_connection;
}

template <core_concepts::exec Exec>
auto basic_request<Exec>::connection() noexcept -> connection_t&
{
	return *m_impl->m_connection;
}

} //namespace riwo::http

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_HTTP_SERVER_DETAIL_REQUEST_H
