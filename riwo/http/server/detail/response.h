// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_DETAIL_RESPONSE_H
#define RIWO_HTTP_SERVER_DETAIL_RESPONSE_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <riwo/http/protocol/utils/server/generator.h>
#include <riwo/http/protocol/utils/core/compression.h>
#include <riwo/http/protocol/utils/core/conditional.h>
#include <riwo/http/protocol/utils/core/upgrade.h>
#include <riwo/http/protocol/utils/core/range.h>
#include <riwo/core/shared_mutex.h>

namespace riwo::http
{

template <core_concepts::exec Exec>
class RIWO_HTTP_TAPI basic_response<Exec>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY(impl)
	using generator_t = server_generator;

	struct range_value : file_range {
		std::string cr_line {};
	};

	class static_file_cache
	{
		static constexpr size_t max_file_size = 2 * 1024 * 1024;
		static constexpr size_t max_total_size = 32 * 1024 * 1024;

		using file_time_t = std::filesystem::file_time_type;
		using order_list = std::list<std::filesystem::path>;

		struct entry
		{
			size_t source_size = 0;
			file_time_t modified {};

			std::shared_ptr<const std::string> source {};
			std::shared_ptr<const std::string> gzip {};
			order_list::iterator order_position {};

			std::shared_ptr<std::atomic_bool> referenced {
				std::make_shared<std::atomic_bool>(true)
			};
		};

	public:
		[[nodiscard]] static bool cacheable(size_t size) noexcept {
			return size > 0 and size <= max_file_size;
		}

		[[nodiscard]] std::shared_ptr<const std::string> find
		(const std::filesystem::path &path, size_t source_size, file_time_t modified, bool gzip) const
		{
			std::shared_lock lock(m_mutex);
			auto pos = m_entries.find(path);

			if( pos == m_entries.end() or pos->second.source_size != source_size or
				pos->second.modified != modified )
				return {};

			pos->second.referenced->store(true, std::memory_order_relaxed);
			return gzip ? pos->second.gzip : pos->second.source;
		}

		void store_source(const std::filesystem::path &path, size_t source_size,
			file_time_t modified, const std::shared_ptr<const std::string> &source)
		{
			if( not source )
				return ;

			std::unique_lock lock(m_mutex);
			erase_locked(path);

			m_order.emplace_back(path);
			auto order_position = std::prev(m_order.end());
			try {
				m_entries.emplace(path, entry {
					.source_size = source_size,
					.modified = modified,
					.source = std::move(source),
					.gzip = {},
					.order_position = order_position,
					.referenced = std::make_shared<std::atomic_bool>(true)
				});
			}
			catch(...)
			{
				m_order.erase(order_position);
				return ;
			}
			m_total_size += source_size;
			evict_locked();
		}

		void store_gzip(const std::filesystem::path &path, size_t source_size,
			file_time_t modified, std::shared_ptr<const std::string> gzip)
		{
			if( not gzip )
				return ;

			std::unique_lock lock(m_mutex);
			auto pos = m_entries.find(path);

			if( pos == m_entries.end() or pos->second.source_size != source_size or
				pos->second.modified != modified )
				return ;

			if( pos->second.gzip )
				m_total_size -= pos->second.gzip->size();
			m_total_size += gzip->size();

			pos->second.gzip = std::move(gzip);
			pos->second.referenced->store(true, std::memory_order_relaxed);
			evict_locked();
		}

	private:
		void erase_locked(const std::filesystem::path &path)
		{
			auto pos = m_entries.find(path);
			if( pos == m_entries.end() )
				return ;

			m_total_size -= pos->second.source ? pos->second.source->size() : 0;
			m_total_size -= pos->second.gzip ? pos->second.gzip->size() : 0;

			m_order.erase(pos->second.order_position);
			m_entries.erase(pos);
		}

		void evict_locked()
		{
			while( m_total_size > max_total_size and not m_order.empty() )
			{
				auto pos = m_entries.find(m_order.front());
				if( pos != m_entries.end() and m_order.size() > 1 and
					pos->second.referenced->exchange(false, std::memory_order_relaxed) )
				{
					m_order.splice(m_order.end(), m_order, pos->second.order_position);
					continue;
				}
				erase_locked(m_order.front());
			}
		}

		std::unordered_map<std::filesystem::path,entry> m_entries {};
		order_list m_order {};
		size_t m_total_size = 0;

		// Cache fills and eviction allocate, release payloads and may walk the
		// LRU list, while lookups benefit from concurrent readers.
		mutable shared_mutex m_mutex {};
	};

public:
	explicit impl(connection_ptr conn, std::filesystem::path resource_root) :
		m_connection(std::move(conn)),
		m_resource_root(std::move(resource_root)) {}

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
		if( framed_size - std::min(framed_size, body_size) <= 2 )
			return 0;
		return framed_size - body_size - 2;
	}

	[[nodiscard]] static size_t logical_body_bytes(size_t wire_bytes, size_t body_offset,
		size_t representation_size, size_t logical_size, bool transformed = false) noexcept
	{
		if( not transformed )
		{
			return body_bytes_transferred (
				wire_bytes, body_offset,
				std::min(representation_size, logical_size)
			);
		}
		if( wire_bytes <= body_offset or
			wire_bytes - body_offset < representation_size )
			return 0;
		return logical_size;
	}

	[[nodiscard]] size_t write_representation(const const_buffer &representation,
		size_t logical_offset, size_t logical_size, error_code &error) noexcept
	{
		if( m_generator.pro_state() == generator_state::content_length )
		{
			auto body = m_generator.body_buffer(representation);
			auto wire_bytes = base_write(body, error);

			return body_bytes_transferred(wire_bytes, logical_offset,
				std::min(logical_size, body.size() > logical_offset ?
					body.size() - logical_offset : size_t{0})
			);
		}
		auto content = m_generator.body_data(representation);
		auto representation_size = representation.size();

		auto offset = framed_body_offset(content.size(), representation_size) +
			logical_offset;

		auto wire_bytes = base_write(std::move(content), error);

		return body_bytes_transferred(wire_bytes, offset,
			std::min(logical_size, representation_size > logical_offset ?
				representation_size - logical_offset : size_t{0}
			)
		);
	}

public:
	[[nodiscard]] size_t write(const_buffer body, error_code &error) noexcept
	{
		error.clear();
		auto pro_state = m_generator.pro_state();

		std::string compressed {};
		const_buffer wire_body = body;
		bool transformed = false;

		if( pro_state == generator_state::finish )
		{
			error = make_error_code(errc::eof);
			return 0;
		}
		else if( pro_state == generator_state::header )
		{
			auto content_type = m_generator.header(header::content_type)
				.transform([](const value &item) { return item.to_string(); })
				.value_or("application/octet-stream");

			if( m_auto_compression and
				gzip_candidate(content_type, body.size(), false) )
				add_vary_accept_encoding();

			if( should_gzip(content_type, body.size(), false) )
			{
				auto encoded = gzip_compress({
					static_cast<const char*>(body.data()), body.size()
				});
				if( not encoded )
				{
					error = encoded.error();
					return 0;
				}
				if( encoded->size() < body.size() or m_req_method == method::head )
				{
					compressed = std::move(*encoded);
					wire_body = buffer(compressed);
					transformed = true;
					prepare_gzip_headers(false);
				}
			}
			auto header = m_generator.header_data(wire_body.size(), m_req_method);
			if( wire_body.size() > 0 and m_generator.pro_state() != generator_state::finish )
			{
				if( m_generator.pro_state() == generator_state::content_length )
				{
					auto content = m_generator.body_buffer(wire_body);
					auto offset = header.size();
					auto wire_bytes = base_write(std::move(header), content, error);

					return logical_body_bytes(wire_bytes, offset,
						transformed ? wire_body.size() : content.size(),
						body.size(), transformed
					);
				}
				auto content = m_generator.body_data(wire_body);
				auto representation_size = wire_body.size();

				auto offset = header.size() +
					framed_body_offset(content.size(), representation_size);

				auto wire_bytes = base_write (
					std::move(header), std::move(content), error
				);
				return logical_body_bytes(wire_bytes, offset,
					representation_size, body.size(), transformed);
			}
			ignore_unused(base_write(std::move(header), error));
			return 0;
		}
		if( wire_body.size() > 0 and m_generator.pro_state() != generator_state::finish )
			return write_body(wire_body, error);
		return 0;
	}

private:
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_write(std::shared_ptr<impl> self, const_buffer body, std::shared_ptr<void> owner)
	{
		ignore_unused(owner);
		auto pro_state = self->m_generator.pro_state();

		size_t sum = 0;
		std::string compressed {};
		const_buffer wire_body = body;
		bool transformed = false;

		if( pro_state == generator_state::finish )
		{
			co_return std::tuple {
				make_error_code(errc::eof), sum
			};
		}
		if( pro_state == generator_state::header )
		{
			auto content_type = self->m_generator.header(header::content_type)
				.transform([](const value &item) { return item.to_string(); })
				.value_or("application/octet-stream");

			if( self->m_auto_compression and self->gzip_candidate(content_type, body.size(), false) )
				self->add_vary_accept_encoding();

			if( self->should_gzip(content_type, body.size(), false) )
			{
				auto encoded = gzip_compress ({
					static_cast<const char*>(body.data()), body.size()
				});
				if( not encoded )
				{
					co_return std::tuple {
						encoded.error(), sum
					};
				}
				if( encoded->size() < body.size() or self->m_req_method == method::head )
				{
					compressed = std::move(*encoded);
					wire_body = buffer(compressed);
					transformed = true;
					self->prepare_gzip_headers(false);
				}
			}
		}
		if( pro_state == generator_state::header )
		{
			auto header_data = self->m_generator.header_data (
				wire_body.size(), self->m_req_method
			);
			if( wire_body.size() > 0 and self->m_generator.pro_state() != generator_state::finish )
			{
				if( self->m_generator.pro_state() == generator_state::content_length )
				{
					auto content = self->m_generator.body_buffer(wire_body);
					auto offset = header_data.size();

					auto [error, bytes] = co_await co_base_write (
						self, std::move(header_data), content
					);
					co_return std::tuple<error_code,size_t> {
						error, logical_body_bytes(bytes, offset,
							transformed ? wire_body.size() : content.size(),
							body.size(), transformed
						)
					};
				}
				auto content = self->m_generator.body_data(wire_body);
				auto representation_size = wire_body.size();

				auto offset = header_data.size() +
					framed_body_offset(content.size(), representation_size);

				auto [error, bytes] = co_await co_base_write (
					self, std::move(header_data), std::move(content)
				);
				co_return std::tuple<error_code,size_t> {
					error, logical_body_bytes(bytes, offset, representation_size,
						body.size(), transformed)
				};
			}
			auto [error, bytes] = co_await co_base_write (
				self, std::move(header_data)
			);
			ignore_unused(bytes);
			co_return std::tuple<error_code,size_t>{error, 0};
		}
		if( wire_body.size() > 0 and self->m_generator.pro_state() != generator_state::finish )
		{
			auto [error, bytes] = co_await co_write_body(self, wire_body);
			co_return std::tuple<error_code,size_t>{error, bytes};
		}
		co_return std::tuple{ error_code{}, sum };
	}

public:
	template <typename Token>
	[[nodiscard]] auto async_write
	(const_buffer input_body, Token &&token, std::shared_ptr<void> body_owner = {})
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), input_body, owner = std::move(body_owner)]
		<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_write(std::move(self), input_body, std::move(owner)),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

private:
	[[nodiscard]] bool status_allows_representation() const noexcept
	{
		auto response_status = m_generator.status();
		auto code = static_cast<uint16_t>(response_status);

		if( response_status == status::none )
			code = static_cast<uint16_t>(status::ok);

		return not (code >= 100 and code < 200) and
			   response_status != status::no_content and
			   response_status != status::reset_content and
			   response_status != status::not_modified and
			   response_status != status::partial_content and
			   not (m_req_method == method::connect and code >= 200 and code < 300);
	}

	[[nodiscard]] bool gzip_candidate(std::string_view mime, size_t size, bool file) const noexcept
	{
		if( size < 256 or
			not status_allows_representation() or
			not is_compressible_mime_type(mime) or
			m_generator.contains_header(header::content_encoding) or
			header_has_token(m_generator.headers(), header::cache_control, "no-transform") )
			return false;

		if( file )
			return m_req_version >= version::v11;

		return not m_generator.contains_header(header::transfer_encoding);
	}

	[[nodiscard]] bool should_gzip(std::string_view mime, size_t size, bool file) const noexcept
	{
		return m_auto_compression and m_client_accepts_gzip and
			   gzip_candidate(mime, size, file) and (not file or m_req_range.empty());
	}

	void set_gzip_variant_etag() noexcept
	{
		auto it = m_generator.headers().find(header::etag);
		if( it == m_generator.headers().end() )
			return ;

		if( auto tag = parse_entity_tag(it->second.to_string()) )
		{
			m_generator.set_header (
				header::etag, "W/\"" + tag->opaque + "-gzip\""
			);
		}
		else
			m_generator.unset_header(header::etag);
	}

	void add_vary_accept_encoding() noexcept
	{
		auto it = m_generator.headers().find(header::vary);
		if( it == m_generator.headers().end() )
		{
			m_generator.set_header(header::vary, header::accept_encoding);
			return ;
		}
		if( not header_has_token(m_generator.headers(), header::vary, "*") and
			not header_has_token(m_generator.headers(), header::vary, header::accept_encoding) )
		{
			m_generator.set_header(header::vary,
				it->second.to_string() + ", " + header::accept_encoding
			);
		}
	}

	void prepare_gzip_headers(bool streaming) noexcept
	{
		m_generator
		.set_header(header::content_encoding, "gzip")
		.unset_header(header::content_length);

		if( streaming )
			m_generator.set_header(header::transfer_encoding, "chunked");
		else
			m_generator.unset_header(header::transfer_encoding);

		add_vary_accept_encoding();
		set_gzip_variant_etag();
	}

	template <typename Opt>
	[[nodiscard]] static bool precompressed_file(const Opt &token) noexcept
	{
		if( is_precompressed_mime_type(token.mime_type) )
			return true;

		if constexpr( requires { token.file_name; } )
		{
			auto extension = strtls::to_lower(token.file_name.extension().string());
			return extension == ".svgz";
		}
		else
			return false;
	}

	template <typename Opt>
	void prepare_file_encoding(const Opt &token) noexcept
	{
		m_generator.set_header(header::content_type, token.mime_type);
		auto candidate = not precompressed_file(token) and
			gzip_candidate(token.mime_type, token.file_size, true);

		if( m_auto_compression and candidate )
			add_vary_accept_encoding();

		m_file_gzip = candidate and should_gzip (
			token.mime_type, token.file_size, true
		);
		if( m_file_gzip )
			prepare_gzip_headers(true);
	}

	void cancel_file_content_encoding() noexcept
	{
		if( not m_file_gzip )
			return ;
		m_file_gzip = false;

		m_generator
		.unset_header(header::content_encoding)
		.unset_header(header::transfer_encoding)
		.unset_header(header::content_length);
	}

public:
	template <typename Opt>
	[[nodiscard]] size_t send_file(Opt &&opt, error_code &error) noexcept
	{
		error.clear();
		if( m_generator.pro_state() != generator_state::header )
			return 0;

		auto f_token = make_file_opt_token(std::forward<Opt>(opt));
		if( not f_token )
		{
			error = f_token.error();
			return 0;
		}
		set_file_validators(*f_token);
		prepare_file_encoding(*f_token);

		if( auto result = precondition_status(); result != status::none )
		{
			if( result == status::precondition_failed )
				cancel_file_content_encoding();

			auto length = result == status::not_modified and not m_file_gzip ?
				f_token->file_size : 0;

			m_generator.set_status(result);
			if( result == status::not_modified and m_file_gzip )
				m_generator.unset_header(header::content_length);
			else
				m_generator.set_header(header::content_length, length);

			return write_header(length, error);
		}
		if( m_req_method != method::get or m_req_range.empty() or
			not if_range_matches() or f_token->file_size == 0 )
			return default_transfer(*f_token, error);

		auto specifier = parse_range_header(m_req_range);
		if( not specifier or specifier->unit != "bytes" or specifier->ranges.size() > 16 )
			return default_transfer(*f_token, error);

		auto resolved = resolve_byte_ranges(*specifier, f_token->file_size);
		if( resolved.empty() )
		{
			m_generator
			.set_status(status::range_not_satisfiable)
			.set_header(header::accept_ranges, "bytes")
			.set_header(header::content_length, 0)
			.set_header(header::content_range,
				format_unsatisfied_content_range(f_token->file_size)
			);
			return write_header(0, error);
		}
		if( excessive_range_set(resolved, f_token->file_size) )
			return default_transfer(*f_token, error);

		return range_transfer(*f_token,
			make_range_values(resolved, f_token->file_size), error
		);
	}

private:
	template <typename AsyncOpt>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_send_file(std::shared_ptr<impl> self, AsyncOpt opt)
	{
		if( self->m_generator.pro_state() != generator_state::header )
			co_return std::tuple<error_code,size_t>{error_code{}, 0};

		auto file_token = self->make_file_opt_token (
			unwrap_async_argument(opt)
		);
		if( not file_token )
		{
			co_return std::tuple<error_code,size_t> {
				file_token.error(), 0
			};
		}
		self->set_file_validators(*file_token);
		self->prepare_file_encoding(*file_token);

		if( auto result = self->precondition_status(); result != status::none )
		{
			if( result == status::precondition_failed )
				self->cancel_file_content_encoding();

			auto length = result == status::not_modified and
				not self->m_file_gzip ? file_token->file_size : 0;

			self->m_generator.set_status(result);
			if( result == status::not_modified and self->m_file_gzip )
				self->m_generator.unset_header(header::content_length);
			else
				self->m_generator.set_header(header::content_length, length);

			auto [error, bytes] = co_await co_write_header(self, length);
			co_return std::tuple<error_code,size_t> {
				error, bytes
			};
		}
		if( self->m_req_method != method::get or
			self->m_req_range.empty() or not self->if_range_matches() or
			file_token->file_size == 0 )
		{
			auto [error, bytes] = co_await co_default_transfer (
				self, *file_token
			);
			co_return std::tuple<error_code,size_t> {
				error, bytes
			};
		}
		auto specifier = parse_range_header(self->m_req_range);

		if( not specifier or specifier->unit != "bytes" or specifier->ranges.size() > 16 )
		{
			auto [error, bytes] = co_await co_default_transfer (
				self, *file_token
			);
			co_return std::tuple<error_code,size_t> {
				error, bytes
			};
		}
		auto resolved = resolve_byte_ranges (
			*specifier, file_token->file_size
		);
		if( resolved.empty() )
		{
			self->m_generator
			.set_status(status::range_not_satisfiable)
			.set_header(header::accept_ranges, "bytes")
			.set_header(header::content_length, 0)
			.set_header(header::content_range,
				format_unsatisfied_content_range(file_token->file_size)
			);
			auto [error, bytes] = co_await co_write_header(self, 0);
			co_return std::tuple<error_code,size_t>{
				error, bytes
			};
		}
		if( self->excessive_range_set(resolved, file_token->file_size) )
		{
			auto [error, bytes] = co_await co_default_transfer (
				self, *file_token
			);
			co_return std::tuple<error_code,size_t> {
				error, bytes
			};
		}
		auto ranges = self->make_range_values (
			resolved, file_token->file_size
		);
		auto [error, bytes] = co_await co_range_transfer (
			self, *file_token, std::move(ranges)
		);
		co_return std::tuple<error_code,size_t> {error, bytes};
	}

public:
	template <typename AsyncOpt, typename Token>
	[[nodiscard]] auto async_send_file(AsyncOpt async_opt, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), opt = opt_t(std::move(async_opt))]
		<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_send_file(std::move(self), std::move(opt)),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	[[nodiscard]] size_t chunk_end
	(const headers_t &trailing_headers, error_code &error) noexcept
	{
		error.clear();
		if( m_generator.pro_state() != generator_state::chunk )
			return 0;

		auto buf = m_generator.chunk_end_data(trailing_headers);
		if( buf.empty() )
			return 0;

		ignore_unused(base_write(std::move(buf), error));
		return 0;
	}

private:
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_chunk_end(std::shared_ptr<impl> self, headers_t trailing_headers)
	{
		if( self->m_generator.pro_state() != generator_state::chunk )
			co_return std::tuple<error_code,size_t>{error_code{}, 0};

		auto data = self->m_generator.chunk_end_data(trailing_headers);
		if( data.empty() )
			co_return std::tuple<error_code,size_t>{error_code{}, 0};

		auto [error, bytes] = co_await co_base_write(self, std::move(data));
		ignore_unused(bytes);
		co_return std::tuple<error_code,size_t>{error, 0};
	}

public:
	template <typename Token>
	[[nodiscard]] auto async_chunk_end(headers_t completion_headers, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_connection->get_executor();

		return asio::async_initiate<token_t,void(error_code,size_t)>(
		[exec, self = this->shared_from_this(), headers = std::move(completion_headers)]
		<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_chunk_end(std::move(self), std::move(headers)),
				riwo::detail::co_spawn_io_handler<size_t,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

private:
	[[nodiscard]] static static_file_cache &file_cache()
	{
		static static_file_cache cache {};
		return cache;
	}

	template <typename Opt>
	[[nodiscard]] std::shared_ptr<const std::string> cached_file_source(Opt &token) noexcept
	{
		using opt_t = std::remove_cvref_t<Opt>;
		if constexpr( requires { token.file_name; typename opt_t::type; } )
		{
			if constexpr( std::same_as<typename opt_t::type,void> )
			{
				if( token.file_name.empty() or not m_file_modified or
					not static_file_cache::cacheable(token.file_size) )
					return {};

				auto &cache = file_cache();
				if( auto source = cache.find(token.file_name, token.file_size, *m_file_modified, false) )
					return source;
				try {
					auto source = std::make_shared<std::string>(token.file_size, '\0');
					token.stream->clear();
					token.stream->seekg(0);

					token.stream->read(source->data(),
						static_cast<std::streamsize>(source->size())
					);
					auto complete = static_cast<size_t>(token.stream->gcount()) ==
						source->size();

					token.stream->clear();
					token.stream->seekg(0);

					if( not complete or not *token.stream )
						return {};

					std::shared_ptr<const std::string> result = std::move(source);
					cache.store_source (
						token.file_name, token.file_size, *m_file_modified, result
					);
					return result;
				}
				catch(...) {}
				return {};
			}
		}
		return {};
	}

	template <typename Opt>
	[[nodiscard]] std::shared_ptr<const std::string> cached_gzip_file(Opt &token) noexcept
	{
		using opt_t = std::remove_cvref_t<Opt>;
		if constexpr( requires { token.file_name; typename opt_t::type; } )
		{
			if constexpr( std::same_as<typename opt_t::type,void> )
			{
				if( token.file_name.empty() or not m_file_modified or
					not static_file_cache::cacheable(token.file_size) )
					return {};

				auto &cache = file_cache();
				if( auto gzip = cache.find(token.file_name, token.file_size, *m_file_modified, true) )
					return gzip;

				auto source = cached_file_source(token);
				if( not source )
					return {};

				auto encoded = gzip_compress(*source);
				if( not encoded )
					return {};
				try {
					std::shared_ptr<const std::string> result =
						std::make_shared<std::string>(std::move(*encoded));

					cache.store_gzip (
						token.file_name, token.file_size, *m_file_modified, result
					);
					return result;
				}
				catch(...) {}
				return {};
			}
		}
		return {};
	}

	template <typename Opt>
	[[nodiscard]] size_t cached_gzip_transfer(Opt &token,
		const std::shared_ptr<const std::string> &data,
		error_code &error) noexcept
	{
		auto sum = write_header(data->size(), error);
		if( error or m_generator.pro_state() == generator_state::finish )
			return sum;

		auto bytes = write_body(buffer(*data), error);
		if( not error and bytes == data->size() )
			sum += token.file_size;

		else if( not error )
			error = make_system_error_code(std::errc::io_error);

		if( not error )
		{
			if( auto end = m_generator.chunk_end_data({}); not end.empty() )
				ignore_unused(base_write(std::move(end), error));
		}
		return sum;
	}

	template <typename Opt>
	[[nodiscard]] sys_expected<size_t> gzip_file_size(Opt &token) noexcept
	{
		gzip_encoder encoder;
		constexpr size_t buf_size = 64 * 1024;

		char data[buf_size] {};
		size_t result = 0;

		token.stream->clear();
		token.stream->seekg(0);

		if( not *token.stream )
			return sys_unexpected(make_system_error_code(std::errc::io_error));
		for(;;)
		{
			token.stream->read(data, buf_size);
			auto count = token.stream->gcount();

			if( count == 0 )
			{
				if( token.stream->eof() )
					break;
				return sys_unexpected(make_system_error_code(std::errc::io_error));
			}
			auto encoded = encoder.append({
				data, static_cast<size_t>(count)
			});
			if( not encoded )
				return sys_unexpected(encoded.error());

			if( encoded->size() > std::numeric_limits<size_t>::max() - result )
				return sys_unexpected(make_system_error_code(std::errc::value_too_large));

			result += encoded->size();
			if( token.stream->eof() )
				break;

			if( not *token.stream )
				return sys_unexpected(make_system_error_code(std::errc::io_error));
		}
		auto encoded = encoder.append({}, true);
		if( not encoded )
			return sys_unexpected(encoded.error());

		if( encoded->size() > std::numeric_limits<size_t>::max() - result )
			return sys_unexpected(make_system_error_code(std::errc::value_too_large));

		result += encoded->size();
		token.stream->clear();
		token.stream->seekg(0);

		if( not *token.stream )
			return sys_unexpected(make_system_error_code(std::errc::io_error));
		return result;
	}

	template <typename Opt>
	[[nodiscard]] size_t gzip_transfer(Opt &token, error_code &error) noexcept
	{
		m_generator
		.set_status(status::ok)
		.unset_header(header::content_range)
		.set_header(header::accept_ranges, "none")
		.set_header(header::content_type, token.mime_type);

		if( auto cached = cached_gzip_file(token) )
			return cached_gzip_transfer(token, cached, error);

		size_t body_size = 0;
		if( m_req_method == method::head )
		{
			auto expected = gzip_file_size(token);
			if( not expected )
			{
				error = expected.error();
				return 0;
			}
			body_size = *expected;
		}
		auto sum = write_header(body_size, error);
		if( error or m_generator.pro_state() == generator_state::finish )
			return sum;

		gzip_encoder encoder;
		constexpr size_t buf_size = 64 * 1024;

		char data[buf_size] {};
		size_t pending_source = 0;

		token.stream->clear();
		token.stream->seekg(0);

		while( not token.stream->eof() )
		{
			token.stream->read(data, buf_size);
			auto size = static_cast<size_t>(token.stream->gcount());

			if( size == 0 )
			{
				if( not token.stream->eof() )
					error = make_system_error_code(std::errc::io_error);
				break;
			}
			auto encoded = encoder.append({data, size});
			if( not encoded )
			{
				error = encoded.error();
				return sum;
			}
			pending_source += size;
			if( not encoded->empty() )
			{
				auto bytes = write_body(buffer(*encoded), error);
				if( not error and bytes == encoded->size() )
				{
					sum += pending_source;
					pending_source = 0;
				}
				else if( not error )
					error = make_system_error_code(std::errc::io_error);
			}
			if( error )
				return sum;
		}
		if( error )
			return sum;

		auto encoded = encoder.append({}, true);
		if( not encoded )
		{
			error = encoded.error();
			return sum;
		}
		if( not encoded->empty() )
		{
			auto bytes = write_body(buffer(*encoded), error);
			if( not error and bytes == encoded->size() )
			{
				sum += pending_source;
				pending_source = 0;
			}
			else if( not error )
				error = make_system_error_code(std::errc::io_error);
		}

		if( error )
			return sum;

		if( auto end = m_generator.chunk_end_data({}); not end.empty() )
			ignore_unused(base_write(std::move(end), error));
		return sum;
	}

	template <typename Opt>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_gzip_transfer(std::shared_ptr<impl> self, Opt &token)
	{
		self->m_generator
		.set_status(status::ok)
		.unset_header(header::content_range)
		.set_header(header::accept_ranges, "none")
		.set_header(header::content_type, token.mime_type);

		if( auto cached = self->cached_gzip_file(token) )
		{
			auto [error, sum] = co_await co_write_header(self, cached->size());
			if( error or self->m_generator.pro_state() == generator_state::finish )
				co_return std::tuple<error_code,size_t>{error, sum};

			auto [write_error, bytes] = co_await co_write_body (
				self, buffer(*cached)
			);
			if( write_error )
				co_return std::tuple<error_code,size_t>{write_error, sum};

			if( bytes != cached->size() )
			{
				co_return std::tuple<error_code,size_t> {
					make_system_error_code(std::errc::io_error), sum
				};
			}
			sum += token.file_size;
			if( auto end = self->m_generator.chunk_end_data({}); not end.empty() )
			{
				auto [end_error, end_bytes] = co_await co_base_write (
					self, std::move(end)
				);
				ignore_unused(end_bytes);
				if( end_error )
					co_return std::tuple<error_code,size_t>{end_error, sum};
			}
			co_return std::tuple<error_code,size_t>{error_code{}, sum};
		}
		size_t body_size = 0;
		if( self->m_req_method == method::head )
		{
			auto expected = self->gzip_file_size(token);
			if( not expected )
			{
				co_return std::tuple<error_code,size_t>{
					expected.error(), 0
				};
			}
			body_size = *expected;
		}
		auto [error, sum] = co_await co_write_header(self, body_size);
		if( error or self->m_generator.pro_state() == generator_state::finish )
			co_return std::tuple<error_code,size_t>{error, sum};

		gzip_encoder encoder;
		constexpr size_t buf_size = 64 * 1024;

		char data[buf_size] {};
		size_t pending_source = 0;

		token.stream->clear();
		token.stream->seekg(0);

		while( not token.stream->eof() )
		{
			token.stream->read(data, buf_size);
			auto size = static_cast<size_t>(token.stream->gcount());
			if( size == 0 )
			{
				if( not token.stream->eof() )
				{
					co_return std::tuple<error_code,size_t>{
						make_system_error_code(std::errc::io_error), sum
					};
				}
				break;
			}
			auto encoded = encoder.append({data, size});
			if( not encoded )
			{
				co_return std::tuple<error_code,size_t> {
					encoded.error(), sum
				};
			}
			pending_source += size;
			if( not encoded->empty() )
			{
				auto [write_error, bytes] = co_await co_write_body (
					self, buffer(*encoded)
				);
				if( write_error )
					co_return std::tuple<error_code,size_t>{write_error, sum};

				if( bytes != encoded->size() )
				{
					co_return std::tuple<error_code,size_t> {
						make_system_error_code(std::errc::io_error), sum
					};
				}
				sum += pending_source;
				pending_source = 0;
			}
		}
		auto encoded = encoder.append({}, true);
		if( not encoded )
			co_return std::tuple<error_code,size_t>{encoded.error(), sum};

		if( not encoded->empty() )
		{
			auto [write_error, bytes] = co_await co_write_body (
				self, buffer(*encoded)
			);
			if( write_error )
				co_return std::tuple<error_code,size_t>{write_error, sum};

			if( bytes != encoded->size() )
			{
				co_return std::tuple<error_code,size_t> {
					make_system_error_code(std::errc::io_error), sum
				};
			}
			sum += pending_source;
			pending_source = 0;
		}
		if( auto end = self->m_generator.chunk_end_data({}); not end.empty() )
		{
			auto [write_error, bytes] = co_await co_base_write (
				self, std::move(end)
			);
			ignore_unused(bytes);
			if( write_error )
				co_return std::tuple<error_code,size_t>{write_error, sum};
		}
		co_return std::tuple<error_code,size_t>{error_code{}, sum};
	}

	template <typename Opt>
	[[nodiscard]] size_t default_transfer(Opt &token, error_code &error) noexcept
	{
		if( m_file_gzip )
			return gzip_transfer(token, error);

		m_generator
		.set_status(status::ok)
		.unset_header(header::content_range)
		.set_header(header::accept_ranges, "bytes")
		.set_header(header::content_type, token.mime_type);

		auto sum = write_header(token.file_size, error);
		if( error or token.file_size == 0 or m_generator.pro_state() == generator_state::finish )
			return sum;

		if( auto cached = cached_file_source(token) )
			return sum + write_body(buffer(*cached), error);

		token.stream->seekg(0);
		while( not token.stream->eof() )
		{
			constexpr size_t buf_size = 0xFFFF;
			char fr_buf[buf_size] {0};

			token.stream->read(fr_buf, buf_size);
			auto size = static_cast<size_t>(token.stream->gcount());

			if( size == 0 )
				break;

			sum += write_body(buffer(fr_buf, size), error);
			if( error )
				break;
		}
		return sum;
	}

	template <typename Opt>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_default_transfer(std::shared_ptr<impl> self, Opt &token)
	{
		if( self->m_file_gzip )
		{
			auto [error, bytes] = co_await co_gzip_transfer(self, token);
			co_return std::tuple<error_code,size_t>{error, bytes};
		}
		self->m_generator
		.set_status(status::ok)
		.unset_header(header::content_range)
		.set_header(header::accept_ranges, "bytes")
		.set_header(header::content_type, token.mime_type);

		auto [error, sum] = co_await co_write_header(self, token.file_size);
		if( error or token.file_size == 0 or self->m_generator.pro_state() == generator_state::finish )
			co_return std::tuple<error_code,size_t>{error, sum};

		if( auto cached = self->cached_file_source(token) )
		{
			auto [write_error, bytes] = co_await co_write_body (
				self, buffer(*cached)
			);
			co_return std::tuple<error_code,size_t>{write_error, sum + bytes};
		}
		constexpr size_t buf_size = 0xFFFF;
		char data[buf_size] {};
		token.stream->seekg(0);

		while( not token.stream->eof() )
		{
			token.stream->read(data, buf_size);
			auto size = static_cast<size_t>(token.stream->gcount());
			if( size == 0 )
				break;

			auto [write_error, bytes] = co_await co_write_body (
				self, buffer(data, size)
			);
			sum += bytes;
			if( write_error )
				co_return std::tuple<error_code,size_t>{write_error, sum};
		}
		co_return std::tuple<error_code,size_t>{error_code{}, sum};
	}

private:
	[[nodiscard]] size_t range_transfer
	(auto &token, const std::vector<range_value> &ranges, error_code &error)
	{
		m_generator.set_status(status::partial_content);
		if( ranges.size() == 1 )
		{
			auto &range = ranges.back();
			m_generator
			.set_header(header::accept_ranges , "bytes"        )
			.set_header(header::content_type  , token.mime_type)
			.set_header(header::content_length, range.total    )
			.set_header(header::content_range,
				format_content_range(range, token.file_size)
			);
			return send_range(token.stream, "", "", ranges, error);
		}
		using namespace std::chrono;

		auto boundary = std::format("{}_{}",
			uuid::generate().to_string(),
			duration_cast<milliseconds>(
				system_clock::now().time_since_epoch()
			).count()
		);
		m_generator.set_header(header::content_type,
			"multipart/byteranges; boundary=" + boundary
		);
		m_generator.unset_header(header::content_range);

		auto ct_line = std::format("{}: {}",
			header::content_type, token.mime_type
		);
		std::size_t content_length = 0;

		for(auto &range : ranges)
		{
			/*
				--boundary<CR><LF>
				Content-Type: xxx<CR><LF>
				Content-Range: bytes 3-11/96<CR><LF>
				<CR><LF>
				012345678<CR><LF>
				--boundary<CR><LF>
				Content-Type: xxx<CR><LF>
				Content-Range: bytes 0-7/96<CR><LF>
				<CR><LF>
				01235467<CR><LF>
				--boundary--<CR><LF>
			*/
			content_length += 2 + boundary.size() + 2 +  // --boundary<CR><LF>
							  ct_line.size() + 2 +       // Content-Type: xxx<CR><LF>
							  range.cr_line.size() + 2 + // Content-Range: bytes 3-11/96<CR><LF>
							  2 +                        // <CR><LF>
							  range.total + 2;           // 012345678<CR><LF>
		}
		content_length += 2 + boundary.size() + 2 + 2;   // --boundary--<CR><LF>

		m_generator
		.set_header(header::content_length, content_length)
		.set_header(header::accept_ranges , "bytes");

		return send_range (
			token.stream, boundary, ct_line, ranges, error
		);
	}

	template <typename Opt>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_range_transfer(std::shared_ptr<impl> self, Opt &token, std::vector<range_value> ranges)
	{
		self->m_generator.set_status(status::partial_content);

		if( ranges.size() == 1 )
		{
			auto &range = ranges.back();
			self->m_generator
			.set_header(header::accept_ranges , "bytes")
			.set_header(header::content_type  , token.mime_type)
			.set_header(header::content_length, range.total)
			.set_header(header::content_range,
				format_content_range(range, token.file_size)
			);
			auto [error, bytes] = co_await co_send_range (
				self, *token.stream, {}, {}, std::move(ranges)
			);
			co_return std::tuple<error_code,size_t>{error, bytes};
		}
		using namespace std::chrono;

		auto boundary = std::format("{}_{}",
			uuid::generate().to_string(),
			duration_cast<milliseconds>(
				system_clock::now().time_since_epoch()
			).count()
		);
		self->m_generator.set_header(header::content_type,
			"multipart/byteranges; boundary=" + boundary
		);
		self->m_generator.unset_header(header::content_range);

		auto content_type_line = std::format (
			"{}: {}", header::content_type, token.mime_type
		);
		size_t content_length = 0;
		for(const auto &range : ranges)
		{
			content_length += 2 + boundary.size() + 2 +
				content_type_line.size() + 2 +
				range.cr_line.size() + 2 + 2 + range.total + 2;
		}
		content_length += 2 + boundary.size() + 2 + 2;

		self->m_generator
		.set_header(header::content_length, content_length)
		.set_header(header::accept_ranges, "bytes");

		auto [error, bytes] = co_await co_send_range (
			self, *token.stream, std::move(boundary),
			std::move(content_type_line), std::move(ranges)
		);
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

private:
	template <typename FS>
	[[nodiscard]] size_t send_range (
		FS &stream, std::string_view boundary, std::string_view ct_line,
		std::vector<range_value> ranges, error_code &error
	) noexcept
	{
		assert(not ranges.empty());
		auto sum = write_header(0, error);
		if( error )
			return sum;

		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size + 2] {0};

		if( ranges.size() == 1 )
		{
			auto &value = ranges.back();
			stream->seekg(value.begin, std::ios_base::beg);

			while( not stream->eof() )
			{
				if( value.total <= buf_size )
				{
					stream->read(buf, value.total);
					auto size = static_cast<size_t>(stream->gcount());

					sum += write_body(buffer(buf,size), error);
					break;
				}
				stream->read(buf, buf_size);
				auto size = static_cast<size_t>(stream->gcount());

				sum += write_body(buffer(buf,size), error);
				if( error )
					break;
				value.total -= size;
			}
			return sum;
		}
		for(auto &value: ranges)
		{
			auto body = std::format (
				"--{}\r\n"
				"{}\r\n"
				"{}\r\n"
				"\r\n",
				boundary,
				ct_line,
				value.cr_line
			);
			sum += write_representation(buffer(body, body.size()), 0, 0, error);
			if( error )
				return sum;

			stream->seekg(value.begin, std::ios_base::beg);
			while( not stream->eof() )
			{
				if( value.total <= buf_size )
				{
					stream->read(buf, value.total);
					auto size = static_cast<size_t>(stream->gcount());
					if( size == 0 )
						break;

					buf[size + 0] = '\r';
					buf[size + 1] = '\n';

					sum += write_representation (
						buffer(buf, size + 2), 0, static_cast<size_t>(size), error
					);
					if( error )
						return sum;
					break;
				}
				stream->read(buf, buf_size);
				auto size = static_cast<size_t>(stream->gcount());

				sum += write_body(buffer(buf,size), error);
				if( error )
					return sum;
				value.total -= static_cast<size_t>(size);
			}
		}
		auto abuf = std::format("--{}--\r\n", boundary);
		sum += write_representation(buffer(abuf, abuf.size()), 0, 0, error);
		return sum;
	}

	template <typename FS>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_send_range(std::shared_ptr<impl> self, FS &source_stream,
		std::string boundary, std::string content_type_line,
		std::vector<range_value> ranges)
	{
		auto *stream = &source_stream;
		assert(not ranges.empty());

		auto [error, sum] = co_await co_write_header(self, 0);
		if( error )
			co_return std::tuple<error_code,size_t>{error, sum};

		constexpr size_t buf_size = 0xFFFF;
		char buf[buf_size + 2] {};

		if( ranges.size() == 1 )
		{
			auto &value = ranges.back();
			stream->seekg(value.begin, std::ios_base::beg);

			while( not stream->eof() )
			{
				auto wanted = std::min(value.total, buf_size);
				stream->read(buf, wanted);

				auto size = static_cast<size_t>(stream->gcount());
				auto [write_error, bytes] = co_await co_write_body (
					self, buffer(buf, size)
				);
				sum += bytes;
				if( write_error )
				{
					co_return std::tuple<error_code,size_t> {
						write_error, sum
					};
				}
				if( value.total <= buf_size )
					break;
				value.total -= size;
			}
			co_return std::tuple<error_code,size_t>{error_code{}, sum};
		}
		for(auto &value : ranges)
		{
			auto body = std::format (
				"--{}\r\n{}\r\n{}\r\n\r\n",
				boundary, content_type_line, value.cr_line
			);
			auto [body_error, body_bytes] = co_await co_write_representation (
				self, buffer(body), 0, 0
			);
			sum += body_bytes;
			if( body_error )
				co_return std::tuple<error_code,size_t>{body_error, sum};

			stream->seekg(value.begin, std::ios_base::beg);
			while( not stream->eof() )
			{
				auto wanted = std::min(value.total, buf_size);
				stream->read(buf, wanted);

				auto size = static_cast<size_t>(stream->gcount());
				if( size == 0 )
					break;
				auto logical_size = size;

				if( value.total <= buf_size )
				{
					buf[size] = '\r';
					buf[size + 1] = '\n';
					size += 2;
				}
				auto [write_error, bytes] = co_await co_write_representation (
					self, buffer(buf, size), 0, logical_size
				);
				sum += bytes;
				if( write_error )
				{
					co_return std::tuple<error_code,size_t> {
						write_error, sum
					};
				}
				if( value.total <= buf_size )
					break;
				value.total -= size;
			}
		}
		auto end = std::format("--{}--\r\n", boundary);

		auto [end_error, end_bytes] = co_await co_write_representation (
			self, buffer(end), 0, 0
		);
		sum += end_bytes;
		co_return std::tuple<error_code,size_t>{end_error, sum};
	}

private:
	[[nodiscard]] size_t write_header(size_t size, error_code &error) noexcept
	{
		ignore_unused(base_write(m_generator.header_data(size, m_req_method), error));
		return 0;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_write_header(std::shared_ptr<impl> self, size_t size)
	{
		auto data = self->m_generator.header_data(size, self->m_req_method);
		auto [error, bytes] = co_await co_base_write(self, std::move(data));
		ignore_unused(bytes);
		co_return std::tuple<error_code,size_t>{error, 0};
	}

	[[nodiscard]] size_t write_body(const const_buffer &body, error_code &error) noexcept {
		return write_representation(body, 0, body.size(), error);
	}

private:
	[[nodiscard]] size_t base_write(const_buffer data, error_code &error) noexcept
	{
		error.clear();
		auto sum = m_connection->write(data, error);
		if( error )
			ignore_unused(m_connection->close());
		return sum;
	}

	[[nodiscard]] size_t base_write(std::string &&data, error_code &error) noexcept
	{
		error.clear();
		auto sum = m_connection->write(data, error);
		if( error )
			ignore_unused(m_connection->close());
		return sum;
	}

	[[nodiscard]] size_t base_write
	(std::string &&header, const_buffer body, error_code &error) noexcept
	{
		error.clear();
		const const_buffer buffers[] {
			const_buffer(header), body
		};
		auto sum = m_connection->write(buffers, error);
		if( error )
			ignore_unused(m_connection->close());
		return sum;
	}

	[[nodiscard]] size_t base_write
	(std::string &&header, std::string &&body, error_code &error) noexcept
	{
		error.clear();
		const const_buffer buffers[] {
			const_buffer(header), const_buffer(body)
		};
		auto sum = m_connection->write(buffers, error);
		if( error )
			ignore_unused(m_connection->close());
		return sum;
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_base_write(std::shared_ptr<impl> self, const_buffer data)
	{
		auto [error, bytes] = co_await self->m_connection->write (
			data, asio::as_tuple(deferred)
		);
		if( error )
			ignore_unused(self->m_connection->close());
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_base_write(std::shared_ptr<impl> self, std::string data)
	{
		auto [error, bytes] = co_await self->m_connection->write (
			buffer(data), asio::as_tuple(deferred)
		);
		if( error )
			ignore_unused(self->m_connection->close());
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_base_write(std::shared_ptr<impl> self, std::string header_data, const_buffer body)
	{
		const const_buffer buffers[] {buffer(header_data), body};
		auto [error, bytes] = co_await self->m_connection->write (
			buffers, asio::as_tuple(deferred)
		);
		if( error )
			ignore_unused(self->m_connection->close());
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_base_write(std::shared_ptr<impl> self, std::string header_data,
		std::string body)
	{
		const const_buffer buffers[] {buffer(header_data), buffer(body)};
		auto [error, bytes] = co_await self->m_connection->write (
			buffers, asio::as_tuple(deferred)
		);
		if( error )
			ignore_unused(self->m_connection->close());
		co_return std::tuple<error_code,size_t>{error, bytes};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec> co_write_representation
	(std::shared_ptr<impl> self, const_buffer body, size_t body_offset, size_t body_size)
	{
		if( self->m_generator.pro_state() == generator_state::content_length )
		{
			auto content = self->m_generator.body_buffer(body);
			auto [error, bytes] = co_await co_base_write(self, content);

			co_return std::tuple<error_code,size_t> {
				error, body_bytes_transferred(bytes, body_offset,
					std::min(body_size, content.size() > body_offset ?
						content.size() - body_offset : size_t{0}
					)
				)
			};
		}
		auto content = self->m_generator.body_data(body);
		auto offset = framed_body_offset(content.size(), body.size()) + body_offset;
		auto [error, bytes] = co_await co_base_write(self, std::move(content));

		co_return std::tuple<error_code,size_t> {
			error, body_bytes_transferred(bytes, offset,
				std::min(body_size, body.size() > body_offset ?
					body.size() - body_offset : size_t{0}
				)
			)
		};
	}

	[[nodiscard]] static asio::awaitable<std::tuple<error_code,size_t>,Exec>
	co_write_body(std::shared_ptr<impl> self, const_buffer body)
	{
		co_return co_await co_write_representation(
			std::move(self), body, 0, body.size());
	}

private:
	template <typename Opt>
	void set_file_validators(const Opt &token) noexcept
	{
		m_file_modified.reset();
		if constexpr( requires { token.file_name; } )
		{
			if( token.file_name.empty() )
				return ;

			std::error_code error {};
			auto file_time = std::filesystem::last_write_time(token.file_name, error);
			if( error )
				return ;

			m_file_modified = file_time;
			auto modified = std::chrono::time_point_cast<std::chrono::system_clock::duration> (
				file_time - decltype(file_time)::clock::now() + std::chrono::system_clock::now()
			);
			auto seconds = std::chrono::duration_cast<std::chrono::seconds> (
				modified.time_since_epoch()
			).count();

			if( not m_generator.contains_header(header::last_modified) )
				m_generator.set_header(header::last_modified, format_http_date(modified));

			if( not m_generator.contains_header(header::etag) )
			{
				m_generator.set_header(header::etag,
					std::format("W/\"{:x}-{:x}\"", token.file_size, seconds)
				);
			}
		}
	}

	[[nodiscard]] status_enum precondition_status() const noexcept
	{
		switch( evaluate_preconditions(m_req_method, m_req_headers, m_generator.headers(), true) )
		{
		case precondition_result::not_modified:
			return status::not_modified;

		case precondition_result::precondition_failed:
			return status::precondition_failed;

		default: break;
		}
		return status::none;
	}

	[[nodiscard]] bool if_range_matches() const noexcept
	{
		if( m_req_if_range.empty() )
			return true;

		const auto &response_headers = m_generator.headers();
		if( m_req_if_range.starts_with("W/") )
			return false;

		if( m_req_if_range.starts_with('"') )
		{
			auto it = response_headers.find(header::etag);
			return it != response_headers.end() and
				strong_entity_tag_equal(it->second.to_string(), m_req_if_range);
		}
		auto it = response_headers.find(header::last_modified);
		if( it == response_headers.end() )
			return false;

		auto validator = parse_http_date(m_req_if_range);
		auto modified = parse_http_date(it->second.to_string());

		return validator and modified and
			   std::chrono::floor<std::chrono::seconds>(*modified) <=
			   std::chrono::floor<std::chrono::seconds>(*validator);
	}

	[[nodiscard]] static bool excessive_range_set
	(const file_ranges &ranges, size_t complete_length) noexcept
	{
		size_t total = 0;
		for(auto &range : ranges)
		{
			if( range.total > complete_length - total )
				return true;
			total += range.total;
		}
		return false;
	}

	[[nodiscard]] std::vector<range_value>
	make_range_values(const file_ranges &ranges, size_t complete_length) const
	{
		std::vector<range_value> result {};
		result.reserve(ranges.size());

		for(auto &range : ranges)
		{
			range_value value {};
			value.begin = range.begin;
			value.total = range.total;

			value.cr_line = std::format("{}: {}", header::content_range,
				format_content_range(range, complete_length)
			);
			result.emplace_back(std::move(value));
		}
		return result;
	}

	[[nodiscard]] std::filesystem::path resource_file_name(std::filesystem::path file_name) const
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
			is_fstream_v<opt_t,char> or is_ifstream_v<opt_t,char> )
		{
			auto token = http::make_file_opt_token(std::forward<Opt>(opt));
			using token_t = decltype(token);

			if constexpr( requires { token.file_name; } )
				token.file_name = resource_file_name(std::move(token.file_name));

			auto expected = token.init(std::ios::in | std::ios::binary);
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

			auto expected = opt.init(std::ios::in | std::ios::binary);
			if( expected )
				return sys_expected<opt_t>(std::forward<Opt>(opt));

			return sys_expected<opt_t>(sys_unexpected(expected.error()));
		}
	}

public:
	connection_ptr m_connection {};

	std::filesystem::path m_resource_root {};
	generator_t m_generator {};

	method_enum m_req_method = method::get;
	version_enum m_req_version = version::v11;

	headers_t m_req_headers {};
	std::string m_req_range {};
	std::string m_req_if_range {};

	optional <
		std::filesystem::file_time_type
	> m_file_modified {};

	bool m_auto_compression = false;
	bool m_client_accepts_gzip = false;
	bool m_file_gzip = false;
};

template <core_concepts::exec Exec>
basic_response<Exec>::basic_response(connection_ptr conn,
	std::filesystem::path resource_root) :
	mutable_headers<basic_response>(nullptr),
	mutable_cookies<cookie,basic_response>(nullptr),
	mutable_chunk_attributes<basic_response>(nullptr),
	m_impl(std::make_shared<impl>(std::move(conn), std::move(resource_root)))
{
	this->m_headers = &m_impl->m_generator.headers();
	this->m_cookies = &m_impl->m_generator.cookies();
	this->m_chunk_attributes = &m_impl->m_generator.chunk_attributes();
}

template <core_concepts::exec Exec>
basic_response<Exec>::~basic_response() = default;

template <core_concepts::exec Exec>
version_enum basic_response<Exec>::version() const noexcept
{
	return m_impl->m_generator.version();
}

template <core_concepts::exec Exec>
basic_response<Exec> &basic_response<Exec>::set_status(status_enum status)
{
	m_impl->m_generator.set_status(status);
	return *this;
}

template <core_concepts::exec Exec>
basic_response<Exec> &basic_response<Exec>::auto_set(request_t &req)
{
	m_impl->m_req_method = req.method();
	m_impl->m_req_version = req.version();
	m_impl->m_req_headers = req.headers();

	m_impl->m_client_accepts_gzip = req.support_gzip();
	m_impl->m_auto_compression = gzip_available_v;

	m_impl->m_req_range.clear();
	m_impl->m_req_if_range.clear();
	m_impl->m_file_gzip = false;

	if( version() < http::version::v11 )
		return *this;

	auto it = req.headers().find(header::range);
	if( it != req.headers().end() )
		m_impl->m_req_range = it->second.to_string();

	it = req.headers().find(header::if_range);
	if( it != req.headers().end() )
		m_impl->m_req_if_range = it->second.to_string();
	return *this;
}

template <core_concepts::exec Exec>
basic_response<Exec> &basic_response<Exec>::set_auto_compression(bool enabled) noexcept
{
	if constexpr( gzip_available_v )
		m_impl->m_auto_compression = enabled;
	return *this;
}

template <core_concepts::exec Exec>
bool basic_response<Exec>::auto_compression() const noexcept
{
	return m_impl->m_auto_compression;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::write(const const_buffer &body, Token &&token)
	requires task_token_v<Token,size_t>
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->write(body, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto sum = m_impl->write(body, error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_response::write"
			);
		}
		return sum;
	}
	else if constexpr( is_detached_v<token_unbound_t<token_t>> )
	{
		auto owned_data = std::make_shared<std::string>();
		if( body.size() > 0 )
		{
			owned_data->assign (
				static_cast<const char*>(body.data()), body.size()
			);
		}
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, data = std::move(owned_data)]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_write(buffer(*data),
				std::forward<T0>(completion_token), data
			);
		},
		std::forward<Token>(token));
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, body]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_write(body,
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::write(Token &&token)
	requires task_token_v<Token,size_t>
{
	return write({nullptr,0}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename T, typename Token>
auto basic_response<Exec>::send_file(T &&opt, Token &&token)
	requires file_task_token_v<T,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->send_file(std::forward<T>(opt), adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto sum = m_impl->send_file (
			std::forward<T>(opt), error
		);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_response::send_file"
			);
		}
		return sum;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, async_opt = capture_async_argument(std::forward<T>(opt))]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_send_file (
				std::move(async_opt), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::redirect
(core_concepts::text_p<char> auto &&url, redirect_enum redi, Token &&token)
	requires task_token_v<Token,size_t>
{
	m_impl->m_generator.set_redirect(std::forward<decltype(url)>(url), redi);
	return write({nullptr,0}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::redirect(core_concepts::text_p<char> auto &&url, Token &&token)
	requires task_token_v<Token,size_t>
{
	return redirect(std::forward<decltype(url)>(url),
		redirect_enum::moved_permanently, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::continues(Token &&token)
	requires task_token_v<Token,size_t>
{
	this->set_status(http::status::continue_upload);
	return write({nullptr,0}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::chunk_end
(const headers_t &trailing_headers, Token &&token)
	requires task_token_v<Token,size_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto adapted_error = adapt_error_code(token);
		return m_impl->chunk_end(trailing_headers, adapted_error.get());
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto sum = m_impl->chunk_end(trailing_headers, error);
		if( error )
		{
			system_error::loc_throw (
				error, "riwo::http::basic_response::chunk_end"
			);
		}
		return sum;
	}
	else
	{
		return initiate_io<size_t>(get_executor(),
		[impl = m_impl, trailing_headers]<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_chunk_end(std::move(trailing_headers),
				std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_response<Exec>::chunk_end(Token &&token)
	requires task_token_v<Token,size_t>
{
	return chunk_end({}, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
status_enum basic_response<Exec>::status() const noexcept
{
	return m_impl->m_generator.status();
}

template <core_concepts::exec Exec>
bool basic_response<Exec>::is_finished() const noexcept
{
	return m_impl->m_generator.pro_state() == generator_state::finish;
}

template <core_concepts::exec Exec>
basic_response<Exec>::executor_t basic_response<Exec>::get_executor() noexcept
{
	return m_impl->m_connection->get_executor();
}

template <core_concepts::exec Exec>
basic_response<Exec> &basic_response<Exec>::cancel() noexcept
{
	ignore_unused(m_impl->m_connection->cancel());
	return *this;
}

} //namespace riwo::http

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_HTTP_SERVER_DETAIL_RESPONSE_H
