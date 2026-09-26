// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_REQUEST_CONTEXT_H
#define RIWO_HTTP_CLIENT_DETAIL_REQUEST_CONTEXT_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

namespace riwo::http
{

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
class RIWO_HTTP_TAPI basic_request_context<Method,Exec,Version>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY(impl)

public:
	impl(lease_ptr &&lease, url_t url, options opt) :
		m_lease(std::move(lease)),
		m_exec(lease_executor(m_lease)),
		m_generator(std::move(url), std::move(opt.arg), opt.target_form),
		m_reply(new reply_t(m_lease)),
		m_cookie_store(std::move(opt.cookie_store))
	{
		m_reply->parser().set_request_method(Method);
		m_reply->parser().set_automatic_decompression(opt.auto_decompression);
		m_reply->bind_cookie_jar(m_cookie_store, m_generator.url());
	}

private:
	[[nodiscard]] static executor_t lease_executor(const lease_ptr &lease)
	{
		if( not lease or not lease->is_valid() )
			invalid_argument::loc_throw("request context connection lease is invalid");
		return lease->get().get_executor();
	}

public:
	template <core_concepts::tf_opt_token<error_code,size_t> Token>
	[[nodiscard]] auto write(const const_buffer &body, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_error_code_token_v<Token> )
		{
			auto adapted_error = adapt_error_code(token);
			return _write(body, adapted_error.get());
		}
		else if constexpr( is_sync_opt_token_v<Token> )
		{
			error_code error {};
			auto size = _write(body, error);
			if( error )
			{
				system_error::loc_throw (
					error, "riwo::http::basic_request_context::write"
				);
			}
			return size;
		}
		else if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto body_storage = std::make_shared<std::string>();
			if( body.size() > 0 )
			{
				body_storage->assign (
					static_cast<const char*>(body.data()), body.size()
				);
			}
			return initiate_io<size_t>(m_exec,
			[self = this->shared_from_this(), owned_body = std::move(body_storage)]
			<typename T0>(T0 &&completion_token) mutable
			{
				return self->async_write(buffer(*owned_body),
					std::forward<T0>(completion_token),
					owned_body
				);
			},
			std::forward<Token>(token));
		}
		else
		{
			return initiate_io<size_t>(m_exec,
			[self = this->shared_from_this(), body]<typename T0>(T0 &&completion_token) mutable
			{
				return self->async_write(body,
					std::forward<T0>(completion_token)
				);
			},
			std::forward<Token>(token));
		}
	}

public:
	[[nodiscard]] size_t upload_file
	(const body_norms_t &norms, auto &&opt, auto &&progress, error_code &error) noexcept
	{
		error.clear();
		auto token = make_file_opt_token(std::forward<decltype(opt)>(opt));
		if( not token )
		{
			error = token.error();
			return 0;
		}
		char buffer[128 * 1024] {0};
		size_t sum = 0, total = 0;

		auto do_transfer = [&](size_t begin, size_t loc_total)
		{
			token->stream->clear();
			token->stream->seekg(begin, std::ios::beg);

			size_t loc_sum = 0;
			while( loc_sum < loc_total )
			{
				auto wanted = std::min(sizeof(buffer), loc_total - loc_sum);
				token->stream->read(buffer,
					static_cast<std::streamsize>(wanted)
				);
				auto gcount = static_cast<size_t>(token->stream->gcount());
				if( gcount == 0 )
					return make_system_error_code(std::errc::io_error);

				error_code write_error {};
				auto bytes = _write({buffer, gcount}, write_error);

				loc_sum += bytes;
				sum += bytes;

				if( write_error )
					return write_error;

				if( bytes != gcount )
					return make_system_error_code(std::errc::io_error);

				if( auto progress_error = invoke_progress(progress, sum, total) )
					return progress_error;
			}
			return error_code();
		};
		if( norms.index() == 0 or norms.index() == std::variant_npos )
		{
			total = token->file_size;
			auto transfer_error = do_transfer(0, token->file_size);
			token->stream->close();

			if( transfer_error )
			{
				close_connection();
				error = transfer_error;
				return sum;
			}
		}
		else if( norms.index() == 1 )
		{
			auto &range_norms = std::get<range_body_norms>(norms);
			total = range_norms.total;

			auto transfer_error = do_transfer(range_norms.begin, range_norms.total);
			token->stream->close();

			if( transfer_error )
			{
				close_connection();
				error = transfer_error;
				return sum;
			}
		}
		else if( norms.index() == 2 )
		{
			const auto &[boundary, packages] = std::get<multipart_body_norms>(norms);
			for(auto &[package_headers, range] : packages)
				total += range.total;

			for(auto &[package_headers, range] : packages)
			{
				std::string prefix;
				try {
					prefix = std::format("--{}\r\n", boundary);
					for(auto &header : package_headers)
						prefix += std::format("{}\r\n", header);
					prefix += "\r\n";
				}
				catch(...)
				{
					token->stream->close();
					close_connection();
					error = exception_error(std::current_exception());
					return sum;
				}
				error_code write_error {};
				ignore_unused(_write(prefix, write_error));
				if( write_error )
				{
					token->stream->close();
					error = write_error;
					return sum;
				}
				else if( auto transfer_error = do_transfer(range.begin, range.total) )
				{
					token->stream->close();
					close_connection();
					error = transfer_error;
					return sum;
				}
				ignore_unused(_write({"\r\n", 2}, write_error));
				if( write_error )
				{
					token->stream->close();
					error = write_error;
					return sum;
				}
			}
			token->stream->close();
			std::string suffix;
			try {
				suffix = std::format("--{}--\r\n", boundary);
			}
			catch(...)
			{
				close_connection();
				error = exception_error(std::current_exception());
				return sum;
			}
			error_code write_error {};
			ignore_unused(_write(std::move(suffix), write_error));
			if( write_error )
			{
				error = write_error;
				return sum;
			}
		}
		else
		{
			token->stream->close();
			error = make_system_error_code(std::errc::invalid_argument);
			return sum;
		}
		return sum;
	}

	template <typename Progress>
	[[nodiscard]] static asio::awaitable<error_code,executor_t>
	co_invoke_progress(executor_t exec, Progress &progress,
		size_t sum, size_t total)
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
					co_return keep_going ? error_code{} :
						make_error_code(errc::operation_aborted);
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

	template <typename File, typename Progress>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_transfer_file(std::shared_ptr<impl> self, File &file,
		Progress &progress, size_t begin_offset, size_t transfer_length,
		size_t already_completed, size_t total_size)
	{
		char data[128 * 1024] {};

		file.stream->clear();
		file.stream->seekg(begin_offset, std::ios::beg);
		size_t transferred = 0;

		while( transferred < transfer_length )
		{
			auto wanted = std::min(sizeof(data),
				transfer_length - transferred
			);
			file.stream->read(data,
				static_cast<std::streamsize>(wanted)
			);
			auto count = static_cast<size_t>(
				file.stream->gcount()
			);
			if( count == 0 )
			{
				co_return std::tuple {
					make_system_error_code(std::errc::io_error), transferred
				};
			}
			auto [write_error, bytes] = co_await co_write (
				self, const_buffer{data, count}, {}
			);
			transferred += bytes;
			if( write_error )
			{
				co_return std::tuple<error_code,size_t>{
					write_error, transferred
				};
			}
			if( bytes != count )
			{
				co_return std::tuple {
					make_system_error_code(std::errc::io_error), transferred
				};
			}
			auto progress_error = co_await co_invoke_progress (
				self->m_exec, progress, already_completed + transferred,
				total_size
			);
			if( progress_error )
			{
				co_return std::tuple<error_code,size_t> {
					progress_error, transferred
				};
			}
		}
		co_return std::tuple {error_code{}, transferred};
	}

	template <typename AsyncOpt, typename AsyncProgress>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_upload_file(std::shared_ptr<impl> self, body_norms_t upload_norms,
		AsyncOpt opt, AsyncProgress progress)
	{
		auto file_expected = self->make_file_opt_token (
			unwrap_async_argument(opt)
		);
		if( not file_expected )
		{
			co_return std::tuple<error_code,size_t> {
				file_expected.error(), 0
			};
		}
		auto &file = *file_expected;
		auto &progress_callback = unwrap_async_argument(progress);

		size_t sum = 0;
		size_t total = 0;
		error_code transfer_error {};

		auto transfer_range = [&](size_t begin, size_t length)
		{
			return co_transfer_file(self, file, progress_callback,
				begin, length, sum, total
			);
		};
		if( upload_norms.index() == 0 or upload_norms.index() == std::variant_npos )
		{
			total = file.file_size;
			auto [range_error, bytes] = co_await transfer_range (
				0, file.file_size
			);
			sum += bytes;
			transfer_error = range_error;
		}
		else if( upload_norms.index() == 1 )
		{
			const auto &range = std::get<range_body_norms>(upload_norms);
			total = range.total;

			auto [range_error, bytes] = co_await transfer_range (
				range.begin, range.total
			);
			sum += bytes;
			transfer_error = range_error;
		}
		else if( upload_norms.index() == 2 )
		{
			auto &[boundary, packages] = std::get<multipart_body_norms>(upload_norms);
			for(const auto &[package_headers, range] : packages)
				total += range.total;

			for(const auto &[package_headers, range] : packages)
			{
				std::string prefix;
				try {
					prefix = std::format (
						"--{}\r\n", boundary
					);
					for(const auto &field : package_headers)
						prefix += std::format("{}\r\n", field);
					prefix += "\r\n";
				}
				catch(...)
				{
					transfer_error = exception_error (
						std::current_exception()
					);
					break;
				}
				auto [prefix_error, prefix_bytes] = co_await co_write (
					self, buffer(prefix), {}
				);
				ignore_unused(prefix_bytes);
				if( prefix_error )
				{
					transfer_error = prefix_error;
					break;
				}
				auto [range_error, range_bytes] = co_await transfer_range (
					range.begin, range.total
				);
				sum += range_bytes;

				if( range_error )
				{
					transfer_error = range_error;
					break;
				}
				auto [separator_error, separator_bytes] = co_await co_write (
					self, const_buffer{"\r\n", 2}, {}
				);
				ignore_unused(separator_bytes);
				if( separator_error )
				{
					transfer_error = separator_error;
					break;
				}
			}
			if( not transfer_error )
			{
				std::string suffix;
				try {
					suffix = std::format (
						"--{}--\r\n", boundary
					);
				}
				catch(...)
				{
					transfer_error = exception_error (
						std::current_exception()
					);
				}
				if( transfer_error )
				{
					file.stream->close();
					self->close_connection();

					co_return std::tuple {
						transfer_error, sum
					};
				}
				auto [suffix_error, suffix_bytes] = co_await co_write (
					self, buffer(suffix), {}
				);
				ignore_unused(suffix_bytes);
				transfer_error = suffix_error;
			}
		}
		else
			transfer_error = make_system_error_code(std::errc::invalid_argument);

		file.stream->close();
		if( transfer_error )
		{
			self->close_connection();
			co_return std::tuple{transfer_error, sum};
		}
		co_return std::tuple{error_code{}, sum};
	}

	template <typename AsyncOpt, typename AsyncProgress, typename Token>
	[[nodiscard]] auto async_upload_file
	(body_norms_t norms, AsyncOpt async_opt, AsyncProgress async_progress, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using progress_t = std::remove_cvref_t<AsyncProgress>;
		using token_t = std::remove_cvref_t<Token>;

		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), norms = std::move(norms),
		 opt = opt_t(std::move(async_opt)),
		 progress = progress_t(std::move(async_progress))]
		(auto completion_handler) mutable
		{
			using handler_t = decltype(completion_handler);
			riwo::detail::launch_awaitable(exec, co_upload_file(std::move(self), std::move(norms),
				std::move(opt), std::move(progress)),
				riwo::detail::co_spawn_io_handler<size_t,handler_t,decltype(exec)>(
					std::move(completion_handler), exec));
		}, completion_token);
	}

public:
	[[nodiscard]] size_t chunk_end
	(const headers_t &completion_headers, error_code &error) noexcept
	{
		error.clear();
		if( m_generator.pro_state() != generator_state::chunk )
		{
			error = make_system_error_code(std::errc::protocol_error);
			return 0;
		}
		std::string buf;
		try {
			buf = m_generator.chunk_end_data(completion_headers);
		}
		catch(...)
		{
			close_connection();
			error = exception_error(std::current_exception());
			return 0;
		}
		if( buf.empty() )
			return 0;

		ignore_unused(base_write(std::move(buf), error));
		return 0;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_chunk_end(std::shared_ptr<impl> self, headers_t trailing_headers)
	{
		if( self->m_generator.pro_state() != generator_state::chunk )
		{
			co_return std::tuple<error_code,size_t> {
				make_system_error_code(std::errc::protocol_error), 0
			};
		}
		std::string data {};
		try {
			data = self->m_generator.chunk_end_data(trailing_headers);
		}
		catch(...)
		{
			self->close_connection();
			co_return std::tuple<error_code,size_t> {
				exception_error(std::current_exception()), 0
			};
		}
		if( data.empty() )
			co_return std::tuple<error_code,size_t>{error_code{}, 0};

		auto [error, bytes] = co_await co_base_write (
			self, std::move(data)
		);
		ignore_unused(bytes);
		co_return std::tuple<error_code,size_t>{error, 0};
	}

	template <typename Token>
	[[nodiscard]] auto async_chunk_end(headers_t completion_headers, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(),
		 headers = std::move(completion_headers)](auto completion_handler) mutable
		{
			using handler_t = decltype(completion_handler);
			riwo::detail::launch_awaitable(exec, co_chunk_end(std::move(self), std::move(headers)),
				riwo::detail::co_spawn_io_handler<size_t,handler_t,decltype(exec)>(
					std::move(completion_handler), exec));
		}, completion_token);
	}

private:
	[[nodiscard]] static size_t body_bytes_transferred
	(size_t wire_bytes, size_t body_offset, size_t body_size) noexcept
	{
		if( wire_bytes <= body_offset )
			return 0;
		return std::min(wire_bytes - body_offset, body_size);
	}

	[[nodiscard]] static size_t framed_body_offset
	(size_t framed_size, size_t body_size) noexcept
	{
		// Chunk framing is: size-line, body, CRLF. Content-Length framing has
		// no overhead. body_data() only produces these two forms.
		if( framed_size - std::min(framed_size, body_size) <= 2 )
			return 0;
		return framed_size - body_size - 2;
	}

	[[nodiscard]] size_t _write(const_buffer body, error_code &error) noexcept
	{
		error.clear();
		try {
			auto pro_state = m_generator.pro_state();
			if( pro_state == generator_state::finish )
			{
				error = make_error_code(errc::eof);
				return 0;
			}
			if( pro_state == generator_state::header )
			{
				auto header = m_generator.header_data(method_v, body.size());
				if( body.size() > 0 )
				{
					auto content = m_generator.body_data(body);
					auto body_size = std::min(body.size(), content.size());

					auto offset = header.size() +
						framed_body_offset(content.size(), body_size);

					auto wire_bytes = base_write (
						std::move(header), std::move(content), error
					);
					return body_bytes_transferred(wire_bytes, offset, body_size);
				}
				ignore_unused(base_write(std::move(header), error));
				return 0;
			}
			if( body.size() > 0 )
				return write_body(body, error);
			return 0;
		}
		catch(...)
		{
			close_connection();
			error = exception_error(std::current_exception());
		}
		return 0;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_write(std::shared_ptr<impl> self, const_buffer input_body,
		std::shared_ptr<void> body_owner)
	{
		ignore_unused(body_owner);
		try {
			auto protocol_state = self->m_generator.pro_state();
			if( protocol_state == generator_state::finish )
			{
				co_return std::tuple<error_code,size_t> {
					make_error_code(errc::eof), 0
				};
			}
			if( protocol_state == generator_state::header )
			{
				auto header_data = self->m_generator.header_data (
					method_v, input_body.size()
				);
				if( input_body.size() > 0 )
				{
					auto body_data = self->m_generator.body_data(input_body);
					auto body_size = std::min(input_body.size(), body_data.size());

					auto offset = header_data.size() +
						framed_body_offset(body_data.size(), body_size);

					auto [error, bytes] = co_await co_base_write (
						self, std::move(header_data), std::move(body_data)
					);
					co_return std::tuple<error_code,size_t> {
						error, body_bytes_transferred(bytes, offset, body_size)
					};
				}
				auto [error, bytes] = co_await co_base_write (
					self, std::move(header_data)
				);
				ignore_unused(bytes);
				co_return std::tuple<error_code,size_t>{error, 0};
			}
			if( input_body.size() == 0 )
				co_return std::tuple<error_code,size_t>{error_code{}, 0};

			auto body_data = self->m_generator.body_data(input_body);
			auto body_size = std::min(input_body.size(), body_data.size());
			auto offset = framed_body_offset(body_data.size(), body_size);

				auto [error, bytes] = co_await co_base_write (
					self, std::move(body_data)
				);
			co_return std::tuple<error_code,size_t> {
				error, body_bytes_transferred(bytes, offset, body_size)
			};
		}
		catch(...) {}

		self->close_connection();
		co_return std::tuple<error_code,size_t> {
			exception_error(std::current_exception()), 0
		};
	}

	template <typename Token>
	[[nodiscard]] auto async_write
	(const_buffer body, Token &&token, std::shared_ptr<void> owner = {})
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_exec;

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), body,
		 owner = std::move(owner)](auto completion_handler) mutable
		{
			using handler_t = decltype(completion_handler);
			riwo::detail::launch_awaitable(exec, co_write(std::move(self), body, std::move(owner)),
				riwo::detail::co_spawn_io_handler<size_t,handler_t,decltype(exec)>(
					std::move(completion_handler), exec));
		}, completion_token);
	}

private:
	[[nodiscard]] size_t write_header(size_t size, error_code &error) noexcept
	{
		ignore_unused(base_write(m_generator.header_data(method_v, size), error));
		return 0;
	}

	[[nodiscard]] size_t write_body
	(const const_buffer &body, error_code &error) noexcept
	{
		auto content = m_generator.body_data(body);
		auto body_size = std::min(body.size(), content.size());

		auto offset = framed_body_offset(content.size(), body_size);
		auto wire_bytes = base_write(std::move(content), error);

		return body_bytes_transferred(wire_bytes, offset, body_size);
	}

private:
	[[nodiscard]] size_t base_write
	(std::string &&data, error_code &error) noexcept
	{
		error.clear();
		if( not has_connection() )
		{
			error = make_system_error_code(std::errc::not_connected);
			return 0;
		}
		auto size = connection().write(const_buffer(data), error);
		if( error )
			close_connection();
		return size;
	}

	[[nodiscard]] size_t base_write
	(std::string &&header, std::string &&body, error_code &error) noexcept
	{
		error.clear();
		if( not has_connection() )
		{
			error = make_system_error_code(std::errc::not_connected);
			return 0;
		}
		const const_buffer buffers[] {
			const_buffer(header), const_buffer(body)
		};
		auto size = connection().write(buffers, error);
		if( error )
			close_connection();
		return size;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_base_write(std::shared_ptr<impl> self, std::string data)
	{
		if( not self->has_connection() )
		{
			co_return std::tuple<error_code,size_t> {
				make_system_error_code(std::errc::not_connected), 0
			};
		}
		auto [error, bytes] = co_await self->connection().write (
			const_buffer(data), asio::as_tuple(deferred)
		);
		if( error )
			self->close_connection();
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,executor_t>
	co_base_write(std::shared_ptr<impl> self, std::string wire_header,
		std::string wire_body)
	{
		if( not self->has_connection() )
		{
			co_return std::tuple<error_code,size_t> {
				make_system_error_code(std::errc::not_connected), 0
			};
		}
		const const_buffer buffers[] {
			const_buffer(wire_header), const_buffer(wire_body)
		};
		auto [error, bytes] = co_await self->connection().write (
			buffers, asio::as_tuple(deferred)
		);
		if( error )
			self->close_connection();
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] connection_t &connection() noexcept {
		return m_lease->get();
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

private:
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
	auto make_file_opt_token(Opt &&opt) noexcept
	{
		using opt_t = std::remove_cvref_t<Opt>;
		if constexpr( is_any_string_v<opt_t> or std::same_as<opt_t,std::filesystem::path> or
			is_fstream_v<opt_t,char> or is_ifstream_v<opt_t,char> )
		{
			auto token = http::make_file_opt_token(std::forward<Opt>(opt));
			using token_t = decltype(token);

			auto expected = token.init(std::ios::in | std::ios::binary);
			if( expected )
				return sys_expected<token_t>(std::move(token));
			return sys_expected<token_t>(sys_unexpected(expected.error()));
		}
		else
		{
			if( opt.stream->is_open() )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			auto expected = opt.init(std::ios::in | std::ios::binary);
			if( expected )
				return sys_expected<opt_t>(std::forward<Opt>(opt));
			return sys_expected<opt_t>(sys_unexpected(expected.error()));
		}
	}

public:
	lease_ptr m_lease {};
	executor_t m_exec {};

	generator_t m_generator;
	reply_ptr m_reply {};

	std::shared_ptr<cookie_jar> m_cookie_store {};
};

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
basic_request_context<Method,Exec,Version>::basic_request_context(lease_ptr &&lease, url_t url, options opt) :
	mutable_headers<basic_request_context>(nullptr),
	mutable_cookies<value,basic_request_context>(nullptr),
	mutable_chunk_attributes<basic_request_context>(nullptr),
	m_impl(std::make_shared<impl>(std::move(lease), std::move(url), std::move(opt)))
{
	this->m_headers = &m_impl->m_generator.headers();
	this->m_cookies = &m_impl->m_generator.cookies();
	this->m_chunk_attributes = &m_impl->m_generator.chunk_attributes();
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
basic_request_context<Method,Exec,Version>::~basic_request_context() = default;

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_request_context<Method,Exec,Version>::write(Token &&token)
{
	return m_impl->write({}, std::forward<Token>(token));
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_request_context<Method,Exec,Version>::write(const const_buffer &body, Token &&token)
	requires put_or_post
{
	return m_impl->write(body, std::forward<Token>(token));
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <typename T, typename Token>
auto basic_request_context<Method,Exec,Version>::upload_file(body_norms_t norms, T &&opt, Token &&token)
	requires file_task_token_v<T,Token>
{
	return upload_file(std::move(norms),
		std::forward<T>(opt), [](size_t,size_t){}, std::forward<Token>(token)
	);
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <typename T, typename Progress, typename Token>
auto basic_request_context<Method,Exec,Version>::upload_file
(body_norms_t norms, T &&opt, Progress &&progress, Token &&token)
	requires file_task_token_v<T,Token> and concepts::progress_callback<Progress,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->upload_file (
			std::move(norms), std::forward<T>(opt),
			std::forward<Progress>(progress), adapted_error.get()
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto size = m_impl->upload_file (
			std::move(norms), std::forward<T>(opt),
			std::forward<Progress>(progress), error
		);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request_context::upload_file"
			);
		}
		return size;
	}
	else
	{
		return initiate_io<size_t>(get_executor(), [
			impl = m_impl, upload_norms = std::move(norms),
			async_opt = capture_async_argument(std::forward<T>(opt)),
			async_progress = capture_async_argument (
				std::forward<Progress>(progress)
			)
		]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_upload_file (
				std::move(upload_norms), std::move(async_opt), std::move(async_progress),
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}
template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_request_context<Method,Exec,Version>::chunk_end
(const headers_t &completion_headers, Token &&token)
	requires put_or_post
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->chunk_end(completion_headers, adapted_error.get());
	}

	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error {};
		auto size = m_impl->chunk_end(completion_headers, error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_request_context::chunk_end"
			);
		}
		return size;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, trailing_headers = completion_headers]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_chunk_end(std::move(trailing_headers),
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}
template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <core_concepts::tf_opt_token<error_code,size_t> Token>
auto basic_request_context<Method,Exec,Version>::chunk_end(Token &&token)
	requires put_or_post
{
	return chunk_end({}, std::forward<Token>(token));
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
template <typename Token>
auto basic_request_context<Method,Exec,Version>::wait_reply(Token &&token)
	requires task_token_v<Token,status_enum>
{
	return reply()->wait(std::forward<Token>(token));
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::reply() const noexcept -> const_reply_ptr
{
	return m_impl->m_reply;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::reply() noexcept -> reply_ptr
{
	return m_impl->m_reply;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
bool basic_request_context<Method,Exec,Version>::responded() const noexcept
{
	return reply()->valid();
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
basic_request_context<Method,Exec,Version>&
basic_request_context<Method,Exec,Version>::cancel() noexcept
{
	if( m_impl->m_lease and m_impl->m_lease->is_valid() )
	{
		ignore_unused(m_impl->m_lease->get().cancel());
		if( auto released_connection = m_impl->m_lease->take() )
			ignore_unused(released_connection->close());
	}
	return *this;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::url() const noexcept -> const url_t&
{
	return m_impl->m_generator.url();
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::arg() const noexcept -> request_arg_t
{
	return m_impl->m_generator.arg();
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
basic_request_context<Method,Exec,Version>::operator request_arg_t() const noexcept
{
	return arg();
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
consteval method_enum basic_request_context<Method,Exec,Version>::method() noexcept
{
	return method_v;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
consteval version_enum basic_request_context<Method,Exec,Version>::version() noexcept
{
	return version_v;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::lease() const noexcept -> const lease_t&
{
	return *m_impl->m_lease;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::lease() noexcept -> lease_t&
{
	return *m_impl->m_lease;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::generator() const noexcept -> const generator_t&
{
	return m_impl->m_generator;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::generator() noexcept -> generator_t&
{
	return m_impl->m_generator;
}

template <method_enum Method, core_concepts::exec Exec, version_enum Version>
auto basic_request_context<Method,Exec,Version>::get_executor() noexcept -> executor_t
{
	return m_impl->m_exec;
}

} //namespace riwo::http

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_HTTP_CLIENT_DETAIL_REQUEST_CONTEXT_H
