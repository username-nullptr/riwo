// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/core/utils/string_tools.h>
#include <riwo/experimental/http/protocol/duplex_channel.h>
#include <riwo/experimental/http/protocol/v2/extension.h>

#include <utility>

#if RIWO_EXP_HTTP_HTTP2_SUPPORT

#include <riwo/experimental/http/protocol/v2/detail/nghttp2_compat.h>

namespace riwo::experimental::http::v2
{

using experimental::http::detail::body_reader_adapter;
using experimental::http::detail::body_reader_state;
using experimental::http::detail::body_writer_adapter;
using experimental::http::detail::body_writer_state;
using experimental::http::detail::duplex_state;

using experimental::http::detail::make_body_reader_state;
using experimental::http::detail::make_body_writer_state;
using experimental::http::make_body_duplex_state;

using namespace ::riwo::experimental::http::v2::detail;

namespace
{

class v2_session;
using v2_session_ptr = std::shared_ptr<v2_session>;

class v2_stream;
using v2_stream_ptr = std::shared_ptr<v2_stream>;

[[nodiscard]] error_code aborted() noexcept
{
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code protocol_error() noexcept
{
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] bool forbidden_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,5> values {
		"connection", "keep-alive", "proxy-connection",
		"transfer-encoding", "upgrade"
	};
	return std::ranges::find(values, name) != values.end();
}

[[nodiscard]] std::string lower(std::string_view value)
{
	return strtls::to_lower(value);
}

struct nv_block
{
	std::vector<std::string> storage {};
	std::vector<nghttp2_nv> values {};
};

void add_nv(nv_block &block, std::string name, std::string value)
{
	block.storage.emplace_back(std::move(name));
	block.storage.emplace_back(std::move(value));

	auto &stored_name = block.storage[block.storage.size() - 2];
	auto &stored_value = block.storage.back();

	block.values.emplace_back(
		reinterpret_cast<uint8_t *>(stored_name.data()),
		reinterpret_cast<uint8_t *>(stored_value.data()),
		stored_name.size(), stored_value.size(), NGHTTP2_NV_FLAG_NONE);
}

[[nodiscard]] sys_expected<nv_block> request_headers(const request_head &head) noexcept
try
{
	nv_block result {};
	result.storage.reserve((head.headers.size() + 5) * 2);
	result.values.reserve(head.headers.size() + 5);

	add_nv(result, ":method", std::string(head.method.text()));
	if( head.connect_protocol )
	{
		if( head.method.known() != method::connect or head.connect_protocol->empty() )
			return sys_unexpected(make_error_code(errc::invalid_field_value));

		add_nv(result, ":protocol", *head.connect_protocol);
	}
	add_nv(result, ":scheme", head.scheme);
	add_nv(result, ":authority", head.authority);
	add_nv(result, ":path", head.target.empty() ? "/" : head.target);

	for(const auto &[key, value] : head.headers)
	{
		auto name = lower(key);
		if( forbidden_header(name) or (name == "te" and lower(value.to_string()) != "trailers") )
			return sys_unexpected(make_error_code(errc::invalid_field_name));

		add_nv(result, std::move(name), value.to_string());
	}
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

[[nodiscard]] sys_expected<nv_block> trailer_headers(const fields &trailers) noexcept
try
{
	nv_block result {};
	result.storage.reserve(trailers.size() * 2);
	result.values.reserve(trailers.size());

	for(const auto &[key, value] : trailers)
	{
		auto name = lower(key);
		if( name.starts_with(':') or forbidden_header(name) or name == "te" )
			return sys_unexpected(make_error_code(errc::invalid_trailer));

		add_nv(result, std::move(name), value.to_string());
	}
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

[[nodiscard]] error_code validate(const client_session_config &config) noexcept
{
	const auto &value = config.h2;
	constexpr uint32_t max_window = 0x7fffffffU;

	if( value.initial_stream_window == 0 or
		value.initial_stream_window > max_window or
		value.initial_connection_window < 65'535 or
		value.initial_connection_window > max_window or
		value.max_concurrent_streams == 0 or
		value.max_frame_size < 16U * 1024U or
		value.max_frame_size > 16'777'215U or
		value.max_header_list_size == 0 or
		value.inbound_stream_buffer < value.initial_stream_window or
		value.outbound_stream_buffer == 0 or
		value.inbound_connection_buffer < 65'535 or
		value.outbound_connection_buffer == 0 )
		return make_error_code(errc::operation_not_permitted);

	const auto reserved =
		static_cast<uint64_t>(value.initial_stream_window) *
		value.max_concurrent_streams;

	if( reserved > value.inbound_connection_buffer )
		return make_error_code(errc::operation_not_permitted);
	return {};
}

class RIWO_DECL_HIDDEN v2_reader_adapter final : public body_reader_adapter
{
	RIWO_DISABLE_COPY_MOVE(v2_reader_adapter)

public:
	explicit v2_reader_adapter(v2_stream &owner) :
		m_owner(std::addressof(owner)) {}

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer, error_code &error) noexcept override;
	void read_some(mutable_buffer buffer, read_handler_t handler) override;

	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override;
	void trailers(trailers_handler_t handler) override;

	void cancel() noexcept override;

private:
	v2_stream *m_owner = nullptr;
};

class RIWO_DECL_HIDDEN v2_writer_adapter final : public body_writer_adapter
{
	RIWO_DISABLE_COPY_MOVE(v2_writer_adapter)

public:
	explicit v2_writer_adapter(v2_stream &owner) :
		m_owner(std::addressof(owner)) {}

	[[nodiscard]] size_t write_sync(const_buffer buffer, error_code &error) noexcept override;
	void write(const_buffer buffer, write_handler_t handler) override;

	void finish_sync(fields trailers, error_code &error) noexcept override;
	void finish(fields trailers, finish_handler_t handler) override;

	void cancel() noexcept override;

private:
	v2_stream *m_owner = nullptr;
};

class RIWO_DECL_HIDDEN v2_stream final : public session_stream,
	public std::enable_shared_from_this<v2_stream>
{
	RIWO_DISABLE_COPY_MOVE(v2_stream)

public:
	v2_stream(v2_session_ptr session, request_head request, body_spec spec);
	~v2_stream() override;
	void initialize();

public:
	[[nodiscard]] stream_capabilities capabilities() const noexcept override
	{
		return {
			.trailers = true,
			.full_duplex = true,
			.tunnel = m_duplex_opened,
			.extended_connect = m_request.connect_protocol.has_value()
		};
	}
	[[nodiscard]] request_head request() const override {
		return m_request;
	}

	[[nodiscard]] std::shared_ptr<body_reader_state> inbound_body() const noexcept override
	{
		auto *self = const_cast<v2_stream*>(this);
		return {self->shared_from_this(), std::addressof(self->m_inbound)};
	}

	[[nodiscard]] std::shared_ptr<body_writer_state> outbound_body() const noexcept override
	{
		auto *self = const_cast<v2_stream*>(this);
		return {self->shared_from_this(), std::addressof(self->m_outbound)};
	}

	[[nodiscard]] response_head wait_response_sync(error_code &error) noexcept override;
	void wait_response(response_handler_t handler) override;

	[[nodiscard]] duplex_state_ptr open_duplex_sync (
		const optional<response_head> &response, error_code &error
	) noexcept override;

	void open_duplex (
		const optional<response_head> &response, duplex_handler_t handler
	) override;

	void cancel() noexcept override;

public:
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] endpoint_t remote_endpoint(error_code &error) const noexcept override;
	[[nodiscard]] endpoint_t local_endpoint(error_code &error) const noexcept override;

	[[nodiscard]] bool reusable() const noexcept override;
	[[nodiscard]] request_delivery delivery() const noexcept override {
		return m_delivery;
	}

	[[nodiscard]] void *protocol_extension(const std::type_info &type) noexcept override
	{
		return type == typeid(stream_extension) ?
			std::addressof(m_extension) : nullptr;
	}

public:
	v2_session_ptr m_session {};
	request_head m_request {};

	body_spec m_spec;
	int32_t m_id = -1;

	stream_extension m_extension {};

	v2_reader_adapter m_reader_adapter;
	v2_writer_adapter m_writer_adapter;

	body_reader_state m_inbound {};
	body_writer_state m_outbound {};

	std::deque<std::vector<std::byte>> m_inbound_chunks {};
	size_t m_inbound_offset = 0;
	size_t m_inbound_size = 0;
	uint64_t m_inbound_total = 0;

	bool m_remote_end = false;
	fields m_inbound_trailers {};

	optional<mutable_buffer> m_pending_read_buffer {};
	body_reader_adapter::read_handler_t m_pending_read_handler {};

	std::deque<std::vector<std::byte>> m_outbound_chunks {};
	size_t m_outbound_offset = 0;
	size_t m_outbound_size = 0;

	bool m_outbound_finished = false;
	bool m_trailers_submitted = false;
	fields m_outbound_trailers {};

	struct pending_write
	{
		const_buffer buffer {};
		body_writer_adapter::write_handler_t handler {};
	};
	std::deque<pending_write> m_pending_writes {};

	response_head m_response {};
	fields m_header_fields {};

	optional<uint16_t> m_header_status {};
	size_t m_header_bytes = 0;
	size_t m_header_count = 0;

	bool m_collecting_trailers = false;
	bool m_response_ready = false;

	response_handler_t m_response_handler {};
	error_code m_terminal_error {};

	uint32_t m_wire_error = NGHTTP2_NO_ERROR;
	bool m_closed = false;
	bool m_cancelled = false;
	bool m_duplex_opened = false;

	request_delivery m_delivery = request_delivery::not_started;
};

class RIWO_DECL_HIDDEN v2_session final : public client_session,
	public std::enable_shared_from_this<v2_session>
{
	RIWO_DISABLE_COPY_MOVE(v2_session)

public:
	v2_session(connection_ptr transport, client_session_config config,
		asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local) :
		m_exec(transport->get_executor()), m_strand(asio::make_strand(m_exec)),
		m_transport(std::move(transport)), m_config(config),
		m_remote(std::move(remote)), m_local(std::move(local)),
		m_peer_max_streams(m_config.h2.max_concurrent_streams) {}

	~v2_session() override
	{
		if( m_native )
			nghttp2_session_del(m_native);
		if( m_callbacks )
			nghttp2_session_callbacks_del(m_callbacks);
		if( m_option )
			nghttp2_option_del(m_option);
	}

	[[nodiscard]] error_code initialize() noexcept
	{
		if( auto error = validate(m_config) )
			return error;

		if( nghttp2_session_callbacks_new(&m_callbacks) != 0 or nghttp2_option_new(&m_option) != 0 )
			return make_error_code(errc::operation_not_supported);

		nghttp2_session_callbacks_set_on_begin_headers_callback (
			m_callbacks, &v2_session::on_begin_headers
		);
		nghttp2_session_callbacks_set_on_header_callback (
			m_callbacks, &v2_session::on_header
		);
		nghttp2_session_callbacks_set_on_frame_recv_callback (
			m_callbacks, &v2_session::on_frame_recv
		);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback (
			m_callbacks, &v2_session::on_data_chunk
		);
		nghttp2_session_callbacks_set_on_stream_close_callback (
			m_callbacks, &v2_session::on_stream_close
		);
		nghttp2_option_set_no_auto_window_update(m_option, 1);
		nghttp2_option_set_max_deflate_dynamic_table_size(m_option, m_config.h2.max_dynamic_table_size);

		if( nghttp2_session_client_new2(&m_native, m_callbacks, this, m_option) != 0 )
			return make_error_code(errc::operation_not_supported);

		std::array<nghttp2_settings_entry,4> settings {{
			{.settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
				.value = m_config.h2.initial_stream_window
			},
			{.settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
				.value = m_config.h2.max_concurrent_streams
			},
			{.settings_id = NGHTTP2_SETTINGS_MAX_FRAME_SIZE,
				.value = m_config.h2.max_frame_size
			},
			{.settings_id = NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
				.value = static_cast<uint32_t>(std::min<size_t>(
					m_config.h2.max_dynamic_table_size, UINT32_MAX
				))
			}
		}};
		if( nghttp2_submit_settings(m_native, NGHTTP2_FLAG_NONE, settings.data(), settings.size()) != 0 )
			return protocol_error();

		if( m_config.h2.initial_connection_window > 65'535 )
		{
			const auto increment = static_cast<int32_t>(
				m_config.h2.initial_connection_window - 65'535
			);
			if( nghttp2_submit_window_update(m_native, NGHTTP2_FLAG_NONE, 0, increment) != 0 )
				return protocol_error();
		}
		m_transport->set_read_timeout(m_config.timeout.body_idle);
		m_transport->set_write_timeout(m_config.timeout.body_idle);
		return {};
	}

public:
	[[nodiscard]] bool try_reserve_stream() noexcept override
	{
		for(;;)
		{
			if( m_draining or m_closed )
				return false;

			const auto maximum = std::min<size_t>(
				m_peer_max_streams, m_config.h2.max_concurrent_streams
			);
			auto reserved = m_reservations.load();
			if( m_active_streams.load() + reserved >= maximum )
				return false;

			if( m_reservations.compare_exchange_weak(reserved, reserved + 1) )
				return true;
		}
	}

	void release_stream(bool reusable) noexcept override
	{
		auto reserved = m_reservations.load();
		while(
			reserved != 0 and
			not m_reservations.compare_exchange_weak(reserved, reserved - 1)
		) {}
		if( not reusable and m_active_streams == 0 )
			notify_release(false);
	}

	void set_release_handler(std::function<void(bool)> handler) override
	{
		std::lock_guard lock(m_handler_mutex);
		m_release_handler = std::move(handler);
	}

	[[nodiscard]] session_stream_ptr open_sync
	(const request_head &head,const body_spec &spec, error_code &error) noexcept override
	{
		error.clear();
		if( not select_mode(1) )
		{
			error = make_error_code(errc::operation_not_permitted);
			consume_reservation();
			return {};
		}
		if( head.connect_protocol )
		{
			if( not m_config.h2.enable_extended_connect )
			{
				consume_reservation();
				error = make_error_code(errc::operation_not_supported);
				return {};
			}
			while( not m_peer_settings_received )
			{
				if( not drive_read_sync(error) )
				{
					consume_reservation();
					return {};
				}
			}
			if( not m_peer_extended_connect )
			{
				consume_reservation();
				error = make_error_code(errc::operation_not_supported);
				return {};
			}
		}
		return submit(head, spec, error);
	}

	void open(request_head head, body_spec spec, open_handler_t handler) override
	{
		if( not select_mode(2) )
		{
			consume_reservation();
			std::move(handler) (
				make_error_code(errc::operation_not_permitted),
				std::shared_ptr<session_stream> {}
			);
			return ;
		}
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self),
			head = std::move(head), spec = std::move(spec), handler = std::move(handler)
		]() mutable
		{
			self->start_async();
			if( head.connect_protocol )
			{
				if( not self->m_config.h2.enable_extended_connect )
				{
					self->consume_reservation();
					std::move(handler) (
						make_error_code(errc::operation_not_supported),
						std::shared_ptr<session_stream> {}
					);
					return ;
				}
				if( not self->m_peer_settings_received )
				{
					self->m_pending_extended_opens.emplace_back(
						std::move(head), std::move(spec), std::move(handler));
					return ;
				}
				if( not self->m_peer_extended_connect )
				{
					self->consume_reservation();
					std::move(handler) (
						make_error_code(errc::operation_not_supported),
						std::shared_ptr<session_stream> {}
					);
					return ;
				}
			}
			error_code error {};
			auto value = self->submit(std::move(head), spec, error);

			if( not error )
				self->write_pump();

			std::move(handler)(error, std::move(value));
		});
	}

	void graceful_shutdown(shutdown_handler_t handler) override
	{
		m_draining = true;
		if( m_mode == 1 )
		{
			(void)nghttp2_submit_goaway (
				m_native, NGHTTP2_FLAG_NONE, 0, NGHTTP2_NO_ERROR, nullptr, 0
			);
			error_code error {};
			flush_sync(error);

			if( m_active_streams == 0 )
				m_transport->cancel();

			std::move(handler)(error);
			return ;
		}
		if( not select_mode(2) and m_mode != 2 )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted));
			return ;
		}
		auto self = shared_from_this();

		asio::post(m_strand,
		[self = std::move(self), handler = std::move(handler)]() mutable
		{
			self->start_async();
			(void)nghttp2_submit_goaway (
				self->m_native, NGHTTP2_FLAG_NONE, 0, NGHTTP2_NO_ERROR, nullptr, 0
			);
			self->write_pump();

			if( self->m_active_streams == 0 )
				self->m_transport->cancel();

			std::move(handler)(error_code{});
		});
	}

	[[nodiscard]] asio::ip::tcp::endpoint remote_endpoint(error_code &error) const noexcept
	{
		error.clear();
		return m_remote;
	}

	[[nodiscard]] asio::ip::tcp::endpoint local_endpoint(error_code &error) const noexcept
	{
		error.clear();
		return m_local;
	}

	[[nodiscard]] const client_session_config &config() const noexcept {
		return m_config;
	}

	[[nodiscard]] response_head wait_response_sync(v2_stream &value, error_code &error) noexcept
	{
		error.clear();
		while( not value.m_response_ready and not value.m_terminal_error )
		{
			if( not drive_read_sync(error) )
				return {};
		}
		if( value.m_terminal_error )
			error = value.m_terminal_error;

		return error ? response_head{} : value.m_response;
	}

	void wait_response(const std::shared_ptr<v2_stream> &value, session_stream::response_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand,
		[self = std::move(self), value, handler = std::move(handler)]() mutable
		{
			if( value->m_terminal_error )
				std::move(handler)(value->m_terminal_error, response_head{});

			else if( value->m_response_ready )
				std::move(handler)(error_code{}, value->m_response);

			else if( value->m_response_handler )
			{
				std::move(handler) (
					make_error_code(errc::operation_in_progress),
					response_head {}
				);
			}
			else
				value->m_response_handler = std::move(handler);
		});
	}

	[[nodiscard]] body_read_result read_sync
	(v2_stream &value, const mutable_buffer &buffer, error_code &error) noexcept
	{
		error.clear();
		while( value.m_inbound_size == 0 and not value.m_remote_end and not value.m_terminal_error )
		{
			if( not drive_read_sync(error) )
				return {};
		}
		if( value.m_terminal_error )
		{
			error = value.m_terminal_error;
			return {};
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 )
			consume(value, result.bytes);
		return result;
	}

	void read(const v2_stream_ptr &value, const mutable_buffer &buffer,
		body_reader_adapter::read_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand,
		[self = std::move(self), value, buffer, handler = std::move(handler)]() mutable
		{
			if( value->m_terminal_error )
			{
				return std::move(handler) (
					value->m_terminal_error, body_read_result{}
				);
			}
			if( value->m_inbound_size != 0 or value->m_remote_end )
			{
				auto result = self->take_body(*value, buffer);
				if( result.bytes != 0 )
					self->consume(*value, result.bytes);

				return std::move(handler)(error_code{}, result);
			}
			value->m_pending_read_buffer = buffer;
			value->m_pending_read_handler = std::move(handler);
		});
	}

	[[nodiscard]] size_t write_sync
	(v2_stream &value, const const_buffer &buffer, error_code &error) noexcept
	{
		error.clear();
		if( buffer.size() > m_config.h2.outbound_stream_buffer )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			return 0;
		}
		while( value.m_outbound_size + buffer.size() > m_config.h2.outbound_stream_buffer or
			   m_outbound_buffered + buffer.size() > m_config.h2.outbound_connection_buffer )
		{
			flush_sync(error);
			if( error )
				return 0;

			if( value.m_outbound_size + buffer.size() <= m_config.h2.outbound_stream_buffer and
				m_outbound_buffered + buffer.size() <= m_config.h2.outbound_connection_buffer )
				break;

			if( not drive_read_sync(error) )
				return 0;
		}
		enqueue(value, buffer);
		(void)nghttp2_session_resume_data(m_native, value.m_id);

		flush_sync(error);
		return error ? 0 : buffer.size();
	}

	void write(const v2_stream_ptr &value, const const_buffer &buffer,
		body_writer_adapter::write_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand,
		[self = std::move(self), value, buffer, handler = std::move(handler)]() mutable
		{
			if( value->m_terminal_error )
				return std::move(handler)(value->m_terminal_error, 0);

			if( buffer.size() > self->m_config.h2.outbound_stream_buffer )
			{
				return std::move(handler) (
					make_error_code(errc::buffer_limit_exceeded), 0
				);
			}
			if( value->m_outbound_size + buffer.size() > self->m_config.h2.outbound_stream_buffer or
				self->m_outbound_buffered + buffer.size() > self->m_config.h2.outbound_connection_buffer )
			{
				value->m_pending_writes.emplace_back(buffer, std::move(handler));
				return ;
			}
			const auto size = buffer.size();
			self->enqueue(*value, buffer);

			(void)nghttp2_session_resume_data(self->m_native, value->m_id);
			self->write_pump();

			std::move(handler)(error_code{}, size);
		});
	}

	void finish_sync(v2_stream &value, fields trailers, error_code &error) noexcept
	{
		error.clear();
		value.m_outbound_finished = true;
		value.m_outbound_trailers = std::move(trailers);

		(void)nghttp2_session_resume_data(m_native, value.m_id);
		flush_sync(error);
	}

	void finish(const v2_stream_ptr &value, fields trailers, body_writer_adapter::finish_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self),
			value, trailers = std::move(trailers), handler = std::move(handler)
		]() mutable
		{
			if( value->m_terminal_error )
				return std::move(handler)(value->m_terminal_error);

			value->m_outbound_finished = true;
			value->m_outbound_trailers = std::move(trailers);

			(void)nghttp2_session_resume_data(self->m_native, value->m_id);
			self->write_pump();

			std::move(handler)(error_code{});
		});
	}

	void cancel(const std::shared_ptr<v2_stream> &value) noexcept
	{
		if( m_mode == 1 )
		{
			cancel_stream(*value);
			error_code ignored {};
			flush_sync(ignored);
			return ;
		}
		auto self = shared_from_this();

		asio::post(m_strand, [self = std::move(self), value]
		{
			self->cancel_stream(*value);
			self->write_pump();
		});
	}

	void release_buffer_budget(size_t inbound, size_t outbound) noexcept
	{
		if( m_mode == 2 )
		{
			auto self = shared_from_this();
			asio::post(m_strand, [self = std::move(self), inbound, outbound]
			{
				self->m_inbound_buffered -= std::min(self->m_inbound_buffered, inbound);
				self->m_outbound_buffered -= std::min(self->m_outbound_buffered, outbound);
			});
			return ;
		}
		m_inbound_buffered -= std::min(m_inbound_buffered, inbound);
		m_outbound_buffered -= std::min(m_outbound_buffered, outbound);
	}

	[[nodiscard]] fields trailers_sync(v2_stream &value, error_code &error) noexcept
	{
		error.clear();
		if( not value.m_remote_end )
			error = make_error_code(errc::operation_not_permitted);
		return error ? fields{} : value.m_inbound_trailers;
	}

	void trailers(const v2_stream_ptr &value, body_reader_adapter::trailers_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand,
		[self = std::move(self), value, handler = std::move(handler)]() mutable
		{
			ignore_unused(self);
			if( not value->m_remote_end )
			{
				std::move(handler) (
					make_error_code(errc::operation_not_permitted),
					fields {}
				);
			}
			else
				std::move(handler)(error_code{}, value->m_inbound_trailers);
		});
	}

public:
	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_exec;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}

	[[nodiscard]] size_t available_stream_capacity() const noexcept override
	{
		if( m_draining or m_closed )
			return 0;

		const auto maximum = std::min<size_t>(
			m_peer_max_streams, m_config.h2.max_concurrent_streams
		);
		const auto used = m_active_streams.load() + m_reservations.load();
		return used >= maximum ? 0 : maximum - used;
	}

	[[nodiscard]] bool draining() const noexcept override {
		return m_draining;
	}
	[[nodiscard]] bool closed() const noexcept override {
		return m_closed;
	}

private:
	[[nodiscard]] bool select_mode(int desired) noexcept
	{
		int expected = 0;
		return m_mode.compare_exchange_strong(expected, desired) or expected == desired;
	}

	void consume_reservation() noexcept
	{
		auto reserved = m_reservations.load();
		while( reserved != 0 and not m_reservations.compare_exchange_weak(reserved, reserved - 1) ) {}
	}

	[[nodiscard]] std::shared_ptr<v2_stream> submit(request_head head,
		const body_spec &spec, error_code &error) noexcept
	{
		error.clear();
		consume_reservation();
		if( head.connect_protocol and not m_config.h2.enable_extended_connect )
		{
			error = make_error_code(errc::operation_not_supported);
			return {};
		}
		if( m_draining or m_closed or available_stream_capacity() == 0 )
		{
			error = make_error_code(errc::operation_not_permitted);
			return {};
		}
		auto headers = request_headers(head);
		if( not headers )
		{
			error = headers.error();
			return {};
		}
		try {
			auto value = std::make_shared<v2_stream>(
				shared_from_this(), std::move(head), spec
			);
			value->initialize();

			nghttp2_api::data_provider_t provider {};
			nghttp2_api::data_provider_t *provider_ptr = nullptr;

			if( spec.type() != body_spec::mode::empty and spec.type() != body_spec::mode::head )
			{
				provider.source.ptr = value.get();
				provider.read_callback = &v2_session::data_read;
				provider_ptr = &provider;
			}
			const auto stream_id = nghttp2_api::submit_request (
				m_native, nullptr, headers->values.data(), headers->values.size(),
				provider_ptr, value.get()
			);
			if( stream_id < 0 )
			{
				error = make_error_code(errc::stream_protocol_error);
				return {};
			}
			value->m_id = stream_id;
			value->m_extension = stream_extension(stream_id);
			value->m_delivery = request_delivery::possibly_processed;

			m_streams.emplace(stream_id, value);
			++m_active_streams;
			return value;
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return {};
	}

	void start_async()
	{
		if( m_async_started )
			return ;

		m_async_started = true;
		read_loop();
		write_pump();
	}

	void read_loop()
	{
		if( m_reading or m_closed )
			return ;

		m_reading = true;
		auto self = shared_from_this();

		m_transport->read_some(buffer(m_read_buffer),
			asio::bind_executor(m_strand,
			[self = std::move(self)](error_code error, size_t size) mutable
			{
				self->m_reading = false;
				if( error or size == 0 )
				{
					self->fail(error ? error : protocol_error());
					return ;
				}
				const auto consumed = nghttp2_api::session_mem_recv (
					self->m_native, reinterpret_cast<const uint8_t *>(self->m_read_buffer.data()), size
				);
				if( consumed < 0 or static_cast<size_t>(consumed) != size )
				{
					self->fail(protocol_error());
					return ;
				}
				self->write_pump();
				self->read_loop();
			})
		);
	}

	void write_pump()
	{
		if( m_writing or m_closed )
			return ;

		const uint8_t *data = nullptr;
		const auto size = nghttp2_api::session_mem_send(m_native, &data);

		if( size < 0 )
			return fail(protocol_error());

		if( size == 0 )
			return ;

		if( static_cast<size_t>(size) > m_config.h2.outbound_connection_buffer )
			return fail(make_error_code(errc::buffer_limit_exceeded));

		const auto batch_limit = std::min<size_t>(
			64 * 1024, m_config.h2.outbound_connection_buffer
		);
		const auto peer_frame_size = static_cast<size_t>(
			nghttp2_session_get_remote_settings(m_native, NGHTTP2_SETTINGS_MAX_FRAME_SIZE)
		);
		auto expected = static_cast<size_t>(size);
		const uint8_t *write_data = data;

		if( nghttp2_session_want_write(m_native) != 0 and expected + peer_frame_size + 9 <= batch_limit )
		{
			m_write_buffer.assign(data, data + expected);
			while( nghttp2_session_want_write(m_native) != 0 and
				   m_write_buffer.size() + peer_frame_size + 9 <= batch_limit )
			{
				const uint8_t *next = nullptr;
				const auto next_size = nghttp2_api::session_mem_send(m_native, &next);

				if( next_size < 0 )
					return fail(protocol_error());

				if( next_size == 0 )
					break;

				m_write_buffer.insert(m_write_buffer.end(),
					next, next + static_cast<size_t>(next_size)
				);
			}
			write_data = m_write_buffer.data();
			expected = m_write_buffer.size();
		}
		m_writing = true;
		auto self = shared_from_this();

		m_transport->write(const_buffer(write_data, expected), asio::bind_executor(m_strand,
			[self = std::move(self), expected](const error_code &error, size_t written) mutable
			{
				self->m_writing = false;
				if( error or written != expected )
				{
					self->fail(error ? error : protocol_error());
					return ;
				}
				self->write_pump();
			})
		);
	}

	void flush_sync(error_code &error) noexcept
	{
		error.clear();
		for(;;)
		{
			const uint8_t *data = nullptr;
			const auto size = nghttp2_api::session_mem_send(m_native, &data);
			if( size < 0 )
			{
				error = protocol_error();
				return ;
			}
			if( size == 0 )
				return ;

			const auto written = m_transport->write_sync (
				const_buffer(data, static_cast<size_t>(size)), error
			);
			if( error or written != static_cast<size_t>(size) )
			{
				if( not error )
					error = protocol_error();
				return ;
			}
		}
	}

	[[nodiscard]] bool drive_read_sync(error_code &error) noexcept
	{
		flush_sync(error);
		if( error )
			return false;

		const auto size = m_transport->read_some_sync (
			buffer(m_read_buffer), error
		);
		if( error or size == 0 )
		{
			if( not error )
				error = protocol_error();
			fail_sync(error);
			return false;
		}
		const auto consumed = nghttp2_api::session_mem_recv (
			m_native, reinterpret_cast<const uint8_t *>(m_read_buffer.data()), size
		);
		if( consumed < 0 or static_cast<size_t>(consumed) != size )
		{
			error = protocol_error();
			fail_sync(error);
			return false;
		}
		flush_sync(error);
		if( error )
			fail_sync(error);
		return not error;
	}

	void fail(error_code error)
	{
		if( m_closed.exchange(true) )
			return ;

		m_draining = true;
		for(auto &[id, value] : m_streams)
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		while( not m_pending_extended_opens.empty() )
		{
			auto pending = std::move(m_pending_extended_opens.front());
			m_pending_extended_opens.pop_front();

			consume_reservation();
			std::move(pending.handler)(error, std::shared_ptr<session_stream>{});
		}
		m_transport->cancel();
		notify_release(false);
	}

	void fail_sync(error_code error)
	{
		if( m_closed.exchange(true) )
			return ;

		m_draining = true;
		for(auto &[id, value] : m_streams)
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		while( not m_pending_extended_opens.empty() )
		{
			auto pending = std::move(m_pending_extended_opens.front());
			m_pending_extended_opens.pop_front();

			consume_reservation();
			std::move(pending.handler)(error, std::shared_ptr<session_stream>{});
		}
		m_transport->cancel();
		notify_release(false);
	}

	void complete_error(v2_stream &value, const error_code &error)
	{
		if( not value.m_terminal_error )
			value.m_terminal_error = error;

		if( value.m_response_handler )
			std::move(value.m_response_handler)(value.m_terminal_error, response_head{});

		if( value.m_pending_read_handler )
		{
			value.m_pending_read_buffer.reset();
			std::move(value.m_pending_read_handler)(value.m_terminal_error, body_read_result{});
		}
		while( not value.m_pending_writes.empty() )
		{
			auto pending = std::move(value.m_pending_writes.front());
			value.m_pending_writes.pop_front();
			std::move(pending.handler)(value.m_terminal_error, 0);
		}
	}

	void notify_release(bool reusable) noexcept
	{
		std::function<void(bool)> handler;
		{
			std::lock_guard lock(m_handler_mutex);
			handler = m_release_handler;
		}
		try {
			if( handler )
				handler(reusable);
		}
		catch(...) {}
	}

	void receive_body(v2_stream &value, const uint8_t *data, size_t size)
	{
		const auto direct = value.m_pending_read_handler and value.m_pending_read_buffer ?
			std::min(size, value.m_pending_read_buffer->size()) : 0;

		const auto queued = size - direct;
		std::vector<std::byte> remainder(queued);

		if( queued != 0 )
		{
			std::memcpy(remainder.data(), data + direct, queued);
			value.m_inbound_chunks.emplace_back(std::move(remainder));

			value.m_inbound_size += queued;
			m_inbound_buffered += queued;
		}
		value.m_inbound_total += size;
		if( direct == 0 )
			return ;

		auto target = *value.m_pending_read_buffer;
		auto handler = std::move(value.m_pending_read_handler);
		value.m_pending_read_buffer.reset();

		std::memcpy(target.data(), data, direct);
		consume(value, direct);

		std::move(handler)(error_code{}, body_read_result {
			.bytes = direct, .eof = false
		});
	}

	[[nodiscard]] body_read_result take_body(v2_stream &value, const mutable_buffer &buffer)
	{
		size_t copied = 0;
		while( copied < buffer.size() and not value.m_inbound_chunks.empty() )
		{
			auto &chunk = value.m_inbound_chunks.front();
			const auto available = chunk.size() - value.m_inbound_offset;
			const auto count = std::min(available, buffer.size() - copied);

			std::memcpy (
				static_cast<std::byte *>(buffer.data()) + copied,
				chunk.data() + value.m_inbound_offset, count
			);
			copied += count;
			value.m_inbound_offset += count;
			value.m_inbound_size -= count;

			m_inbound_buffered -= count;
			if( value.m_inbound_offset == chunk.size() )
			{
				value.m_inbound_chunks.pop_front();
				value.m_inbound_offset = 0;
			}
		}
		return {
			.bytes = copied,
			.eof = value.m_remote_end and value.m_inbound_size == 0
		};
	}

	void consume(const v2_stream &value, size_t size)
	{
		if( size == 0 or value.m_id <= 0 )
			return ;

		if( nghttp2_session_consume(m_native, value.m_id, size) != 0 )
			return fail(protocol_error());

		if( m_mode == 2 )
			write_pump();
	}

	void enqueue(v2_stream &value, const const_buffer &buffer)
	{
		std::vector<std::byte> bytes(buffer.size());
		if( buffer.size() != 0 )
			std::memcpy(bytes.data(), buffer.data(), buffer.size());
		enqueue(value, std::move(bytes));
	}

	void enqueue(v2_stream &value, std::vector<std::byte> bytes)
	{
		value.m_outbound_size += bytes.size();
		m_outbound_buffered += bytes.size();
		value.m_outbound_chunks.emplace_back(std::move(bytes));
	}

	void drain_pending(v2_stream &value)
	{
		while( not value.m_pending_writes.empty() )
		{
			auto &pending = value.m_pending_writes.front();

			if( value.m_outbound_size + pending.buffer.size() > m_config.h2.outbound_stream_buffer or
				m_outbound_buffered + pending.buffer.size() > m_config.h2.outbound_connection_buffer )
				return ;

			auto item = std::move(pending);
			value.m_pending_writes.pop_front();

			const auto size = item.buffer.size();
			enqueue(value, item.buffer);

			std::move(item.handler)(error_code{}, size);
		}
	}

	void deliver_pending_read(v2_stream &value)
	{
		if( not value.m_pending_read_handler or
			(value.m_inbound_size == 0 and not value.m_remote_end and not value.m_terminal_error) )
			return ;

		auto handler = std::move(value.m_pending_read_handler);
		auto buffer = *value.m_pending_read_buffer;

		value.m_pending_read_buffer.reset();
		if( value.m_terminal_error )
		{
			std::move(handler)(value.m_terminal_error, body_read_result{});
			return ;
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 )
			consume(value, result.bytes);
		std::move(handler)(error_code{}, result);
	}

	void response_ready(v2_stream &value)
	{
		if( not value.m_header_status )
		{
			complete_error(value, make_error_code(errc::stream_protocol_error));
			(void)nghttp2_submit_rst_stream (
				m_native, NGHTTP2_FLAG_NONE, value.m_id, NGHTTP2_PROTOCOL_ERROR
			);
			return ;
		}
		if( *value.m_header_status < 200 )
		{
			value.m_header_fields = {};
			value.m_header_status.reset();
			value.m_header_bytes = 0;
			value.m_header_count = 0;
			return ;
		}
		try {
			value.m_response = {
				.status = status(*value.m_header_status),
				.headers = std::move(value.m_header_fields)
			};
			value.m_response_ready = true;
			if( value.m_response_handler )
				std::move(value.m_response_handler)(error_code{}, value.m_response);
		}
		catch(...) {
			complete_error(value, make_error_code(errc::stream_protocol_error));
		}
	}

	void remote_end(v2_stream &value)
	{
		value.m_remote_end = true;
		deliver_pending_read(value);
	}

	void cancel_stream(v2_stream &value)
	{
		if( value.m_cancelled or value.m_closed )
			return ;

		value.m_cancelled = true;
		complete_error(value, aborted());

		if( value.m_id > 0 )
		{
			(void)nghttp2_submit_rst_stream (
				m_native, NGHTTP2_FLAG_NONE, value.m_id, NGHTTP2_CANCEL
			);
		}
	}

	static int on_begin_headers(nghttp2_session *, const nghttp2_frame *frame, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		if( frame->hd.type != NGHTTP2_HEADERS )
			return 0;

		auto found = self.m_streams.find(frame->hd.stream_id);
		if( found == self.m_streams.end() )
			return 0;

		auto &value = *found->second;
		value.m_collecting_trailers = value.m_response_ready;
		value.m_header_fields = {};

		value.m_header_status.reset();
		value.m_header_bytes = 0;
		value.m_header_count = 0;
		return 0;
	}

	static int on_header(nghttp2_session*, const nghttp2_frame *frame, const uint8_t *name,
		size_t name_size, const uint8_t *field_value, size_t value_size, uint8_t, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		auto found = self.m_streams.find(frame->hd.stream_id);

		if( found == self.m_streams.end() )
			return 0;

		auto &value = *found->second;
		const auto header_limit = std::min (
			self.m_config.h2.max_header_list_size,
			self.m_config.reply_limits.max_header_list_size
		);
		value.m_header_bytes += name_size + value_size + 32;
		++value.m_header_count;

		if( value.m_header_bytes > header_limit or
			value.m_header_count > self.m_config.reply_limits.max_field_count or
			name_size > self.m_config.reply_limits.max_field_name_size or
			value_size > self.m_config.reply_limits.max_field_value_size )
		{
			(void)nghttp2_submit_rst_stream (
				self.m_native, NGHTTP2_FLAG_NONE, value.m_id, NGHTTP2_ENHANCE_YOUR_CALM
			);
			self.complete_error(value,
				make_error_code(errc::header_limit_exceeded)
			);
			return 0;
		}
		const std::string_view header_name (
			reinterpret_cast<const char *>(name), name_size
		);
		const std::string_view header_value (
			reinterpret_cast<const char *>(field_value), value_size
		);
		if( header_name == ":status" )
		{
			uint16_t status = 0;
			const auto parsed = std::from_chars (
				header_value.data(), header_value.data() + header_value.size(), status
			);
			if( value.m_collecting_trailers or value.m_header_status or
				parsed.ec != std::errc{} or
				parsed.ptr != header_value.data() + header_value.size() or
				status < 100 or status > 599 )
				return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

			value.m_header_status = status;
			return 0;
		}
		if( header_name.starts_with(':') or forbidden_header(header_name) or
			(header_name == "te" and lower(header_value) != "trailers") )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		try {
			if( value.m_collecting_trailers )
				value.m_inbound_trailers.add(header_name, header_value);
			else
				value.m_header_fields.add(header_name, header_value);
		}
		catch(...) {
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		}
		return 0;
	}

	static int on_frame_recv(nghttp2_session*, const nghttp2_frame *frame, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		if( frame->hd.type == NGHTTP2_GOAWAY )
		{
			self.m_draining = true;
			for(auto &[id, value] : self.m_streams)
			{
				if( id > frame->goaway.last_stream_id )
				{
					value->m_wire_error = frame->goaway.error_code;
					value->m_delivery = request_delivery::definitely_not_processed;

					self.complete_error(*value,
						make_error_code(errc::stream_protocol_error)
					);
				}
			}
			self.notify_release(false);
			return 0;
		}
		if( frame->hd.type == NGHTTP2_SETTINGS and not (frame->hd.flags & NGHTTP2_FLAG_ACK) )
		{
			const auto peer = nghttp2_session_get_remote_settings (
				self.m_native, NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS
			);
			self.m_peer_max_streams = std::min<uint64_t>(
				peer, self.m_config.h2.max_concurrent_streams
			);
			self.m_peer_settings_received = true;

			self.m_peer_extended_connect = nghttp2_session_get_remote_settings (
				self.m_native, NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL
			) != 0;

			self.resume_extended_opens();
			self.notify_release(true);
			return 0;
		}
		if( frame->hd.type != NGHTTP2_HEADERS and
			frame->hd.type != NGHTTP2_DATA and
			frame->hd.type != NGHTTP2_RST_STREAM )
			return 0;

		auto found = self.m_streams.find(frame->hd.stream_id);
		if( found == self.m_streams.end() )
			return 0;

		auto &value = *found->second;
		if( frame->hd.type == NGHTTP2_HEADERS and not value.m_collecting_trailers )
			self.response_ready(value);

		if( frame->hd.type == NGHTTP2_RST_STREAM )
		{
			value.m_wire_error = frame->rst_stream.error_code;

			if( value.m_wire_error == NGHTTP2_REFUSED_STREAM )
				value.m_delivery = request_delivery::definitely_not_processed;

			self.complete_error(value,
				make_error_code(errc::stream_protocol_error));
		}
		if( frame->hd.flags & NGHTTP2_FLAG_END_STREAM )
			self.remote_end(value);
		return 0;
	}

	static int on_data_chunk
	(nghttp2_session*, uint8_t, int32_t stream_id, const uint8_t *data, size_t size, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		auto found = self.m_streams.find(stream_id);

		if( found == self.m_streams.end() )
			return 0;

		auto &value = *found->second;
		if( value.m_inbound_size + size > self.m_config.h2.inbound_stream_buffer or
			self.m_inbound_buffered + size > self.m_config.h2.inbound_connection_buffer or (
				self.m_config.reply_limits.max_body_size and
				value.m_inbound_total + size > *self.m_config.reply_limits.max_body_size
			) )
		{
			(void)nghttp2_submit_rst_stream (
				self.m_native, NGHTTP2_FLAG_NONE, stream_id, NGHTTP2_ENHANCE_YOUR_CALM
			);
			self.complete_error(value,
				make_error_code(errc::buffer_limit_exceeded)
			);
			return 0;
		}
		try {
			self.receive_body(value, data, size);
		}
		catch(...) {
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}

	static int on_stream_close
	(nghttp2_session*, int32_t stream_id, uint32_t error_code_value, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		auto found = self.m_streams.find(stream_id);

		if( found == self.m_streams.end() )
			return 0;

		auto value = found->second;
		value->m_closed = true;
		value->m_wire_error = error_code_value;

		if( error_code_value != NGHTTP2_NO_ERROR and not value->m_terminal_error )
			self.complete_error(*value, make_error_code(errc::stream_protocol_error));

		if( error_code_value == NGHTTP2_NO_ERROR )
			self.remote_end(*value);

		self.m_streams.erase(found);
		if( self.m_active_streams != 0 )
			--self.m_active_streams;

		self.notify_release(not self.m_draining and not self.m_closed);
		if( self.m_draining and self.m_active_streams == 0 )
			self.m_transport->cancel();
		return 0;
	}

	static nghttp2_api::ssize_type_t data_read(nghttp2_session *session, int32_t, uint8_t *buffer,
		size_t length, uint32_t *flags, nghttp2_data_source *source, void *user_data)
	{
		auto &self = *static_cast<v2_session *>(user_data);
		auto &value = *static_cast<v2_stream *>(source->ptr);

		if( value.m_terminal_error )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

		if( value.m_outbound_size == 0 )
		{
			if( not value.m_outbound_finished )
				return NGHTTP2_ERR_DEFERRED;

			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.m_outbound_trailers.empty() and not value.m_trailers_submitted )
			{
				auto trailers = trailer_headers(value.m_outbound_trailers);
				if( not trailers )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;

				if( nghttp2_submit_trailer(session, value.m_id,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

				value.m_trailers_submitted = true;
			}
			return 0;
		}
		size_t copied = 0;
		while( copied < length and not value.m_outbound_chunks.empty() )
		{
			auto &chunk = value.m_outbound_chunks.front();
			const auto available = chunk.size() - value.m_outbound_offset;

			const auto count = std::min(available, length - copied);
			std::memcpy(buffer + copied, chunk.data() + value.m_outbound_offset, count);

			copied += count;
			value.m_outbound_offset += count;
			value.m_outbound_size -= count;
			self.m_outbound_buffered -= count;

			if( value.m_outbound_offset == chunk.size() )
			{
				value.m_outbound_chunks.pop_front();
				value.m_outbound_offset = 0;
			}
		}
		self.drain_pending(value);

		if( value.m_outbound_size == 0 and value.m_outbound_finished )
		{
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.m_outbound_trailers.empty() and not value.m_trailers_submitted )
			{
				auto trailers = trailer_headers(value.m_outbound_trailers);
				if( not trailers )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;

				if( nghttp2_submit_trailer(session, value.m_id,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

				value.m_trailers_submitted = true;
			}
		}
		return static_cast<nghttp2_api::ssize_type_t>(copied);
	}

	struct pending_extended_open
	{
		request_head head {};
		body_spec spec;
		open_handler_t handler {};
	};

	void resume_extended_opens()
	{
		while( not m_pending_extended_opens.empty() )
		{
			auto pending = std::move(m_pending_extended_opens.front());
			m_pending_extended_opens.pop_front();

			if( not m_peer_extended_connect )
			{
				consume_reservation();
				std::move(pending.handler) (
					make_error_code(errc::operation_not_supported),
					std::shared_ptr<session_stream> {}
				);
				continue;
			}
			error_code error {};
			auto value = submit(std::move(pending.head), pending.spec, error);
			std::move(pending.handler)(error, std::move(value));
		}
		write_pump();
	}

	executor_t m_exec {};
	asio::strand<asio::any_io_executor> m_strand {};

	connection_ptr m_transport {};
	client_session_config m_config {};

	asio::ip::tcp::endpoint m_remote {};
	asio::ip::tcp::endpoint m_local {};

	nghttp2_session_callbacks *m_callbacks = nullptr;
	nghttp2_option *m_option = nullptr;
	nghttp2_session *m_native = nullptr;

	std::unordered_map<int32_t,std::shared_ptr<v2_stream>> m_streams {};
	std::array<std::byte,16 * 1024> m_read_buffer {};
	std::vector<uint8_t> m_write_buffer {};

	size_t m_inbound_buffered = 0;
	size_t m_outbound_buffered = 0;

	std::atomic<size_t> m_active_streams {0};
	std::atomic<size_t> m_reservations {0};
	std::atomic<size_t> m_peer_max_streams;

	std::atomic<bool> m_draining {false};
	std::atomic<bool> m_closed {false};

	std::atomic<int> m_mode {0};
	std::deque<pending_extended_open> m_pending_extended_opens {};

	bool m_peer_settings_received = false;
	bool m_peer_extended_connect = false;

	bool m_async_started = false;
	bool m_reading = false;
	bool m_writing = false;

	std::mutex m_handler_mutex {};
	std::function<void(bool)> m_release_handler {};
};

v2_stream::v2_stream(std::shared_ptr<v2_session> session, request_head request, body_spec spec) :
	m_session(std::move(session)), m_request(std::move(request)), m_spec(std::move(spec)),
	m_reader_adapter(*this), m_writer_adapter(*this),
	m_inbound(m_reader_adapter, false, m_session->config().reply_limits.max_discard_body),
	m_outbound(m_writer_adapter, m_spec, true)
{

}

v2_stream::~v2_stream()
{
	if( m_session )
		m_session->release_buffer_budget(m_inbound_size, m_outbound_size);
}

void v2_stream::initialize()
{
	auto lifetime = weak_from_this();
	m_inbound.bind_lifetime(lifetime);
	m_outbound.bind_lifetime(lifetime);
}

response_head v2_stream::wait_response_sync(error_code &error) noexcept
{
	return m_session->wait_response_sync(*this, error);
}

void v2_stream::wait_response(response_handler_t handler)
{
	m_session->wait_response(shared_from_this(), std::move(handler));
}

auto v2_stream::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	return m_session->remote_endpoint(error);
}

auto v2_stream::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	return m_session->local_endpoint(error);
}

bool v2_stream::reusable() const noexcept
{
	return m_closed and not m_terminal_error and not m_session->draining();
}

std::shared_ptr<duplex_state> v2_stream::open_duplex_sync
(const optional<response_head> &response, error_code &error) noexcept
{
	ignore_unused(response);
	error.clear();

	if( m_duplex_opened or not m_response_ready or not m_request.connect_protocol or
		m_response.status.code() < 200 or m_response.status.code() >= 300 )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	m_duplex_opened = true;
	error_code endpoint_error {};

	auto remote = m_session->remote_endpoint(endpoint_error);
	endpoint_error.clear();

	auto local = m_session->local_endpoint(endpoint_error);
	auto self = shared_from_this();

	return make_body_duplex_state(m_session->get_executor(),
		protocol::v2, inbound_body(), outbound_body(), remote, local, self,
		[weak = weak_from_this()]
		{
			if( auto value = weak.lock() )
				value->m_session->cancel(value);
		}
	);
}

void v2_stream::open_duplex(const optional<response_head> &response, duplex_handler_t handler)
{
	error_code error {};
	auto result = open_duplex_sync(response, error);
	std::move(handler)(error, std::move(result));
}

void v2_stream::cancel() noexcept
{
	if( m_duplex_opened )
		return ;

	if( m_session )
		m_session->cancel(shared_from_this());
}

body_read_result v2_reader_adapter::read_some_sync(mutable_buffer buffer, error_code &error) noexcept
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
	{
		error = aborted();
		return {};
	}
	return owner->m_session->read_sync(*owner, buffer, error);
}

void v2_reader_adapter::read_some(mutable_buffer buffer, read_handler_t handler)
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
		return std::move(handler)(aborted(), body_read_result{});

	owner->m_session->read(owner, buffer, std::move(handler));
}

fields v2_reader_adapter::trailers_sync(error_code &error) noexcept
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
	{
		error = aborted();
		return {};
	}
	return owner->m_session->trailers_sync(*owner, error);
}

void v2_reader_adapter::trailers(trailers_handler_t handler)
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
		return std::move(handler)(aborted(), fields{});

	owner->m_session->trailers(owner, std::move(handler));
}

void v2_reader_adapter::cancel() noexcept
{
	if( auto owner = m_owner ? m_owner->weak_from_this().lock() : nullptr )
		owner->cancel();
}

size_t v2_writer_adapter::write_sync(const_buffer buffer, error_code &error) noexcept
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
	{
		error = aborted();
		return 0;
	}
	return owner->m_session->write_sync(*owner, buffer, error);
}

void v2_writer_adapter::write(const_buffer buffer, write_handler_t handler)
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
		return std::move(handler)(aborted(), 0);

	owner->m_session->write(owner, buffer, std::move(handler));
}

void v2_writer_adapter::finish_sync(fields trailers, error_code &error) noexcept
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
	{
		error = aborted();
		return ;
	}
	owner->m_session->finish_sync(*owner, std::move(trailers), error);
}

void v2_writer_adapter::finish(fields trailers, finish_handler_t handler)
{
	auto owner = m_owner ?
		m_owner->weak_from_this().lock() : nullptr;

	if( not owner )
		return std::move(handler)(aborted());

	owner->m_session->finish (
		owner, std::move(trailers), std::move(handler)
	);
}

void v2_writer_adapter::cancel() noexcept
{
	if( auto owner = m_owner ? m_owner->weak_from_this().lock() : nullptr )
		owner->cancel();
}

} //namespace

sys_expected<client_session_ptr>
make_client_session(connection_ptr transport, const client_session_config &config,
	const asio::ip::tcp::endpoint &remote, const asio::ip::tcp::endpoint &local)
{
	if( not transport )
		return sys_unexpected(make_error_code(errc::negotiation_failed));

	auto result = std::make_shared<v2_session>(
		std::move(transport), config, remote, local
	);
	if( auto error = result->initialize() )
		return sys_unexpected(error);

	return std::static_pointer_cast<client_session>(std::move(result));
}

} //namespace riwo::experimental::http::v2


#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
