// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/core/utils/string_tools.h>
#include <riwo/http_exp/protocol/duplex_channel.h>

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

#include "nghttp2_compat.h"
#include <charconv>
#include <deque>

namespace riwo::http_exp::v2::detail
{
using ::riwo::http_exp::detail::body_reader_adapter;
using ::riwo::http_exp::detail::body_reader_state;
using ::riwo::http_exp::detail::body_writer_adapter;
using ::riwo::http_exp::detail::body_writer_state;
using ::riwo::http_exp::detail::duplex_state;
using ::riwo::http_exp::detail::make_body_reader_state;
using ::riwo::http_exp::detail::make_body_duplex_state;
using ::riwo::http_exp::detail::make_body_writer_state;
using ::riwo::http_exp::detail::server_accept_handler_t;
using ::riwo::http_exp::detail::server_connection;
using ::riwo::http_exp::detail::server_connection_ptr_t;
using ::riwo::http_exp::detail::server_session;
using ::riwo::http_exp::detail::stream;
using ::riwo::http_exp::detail::validate_server_config;

namespace
{

class server_session_impl;
class server_stream_impl;

[[nodiscard]] error_code aborted() noexcept {
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] std::string lower(std::string_view value)
{
	return strtls::to_lower(value);
}

[[nodiscard]] bool forbidden_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,5> values {
		"connection", "keep-alive", "proxy-connection",
		"transfer-encoding", "upgrade"
	};
	return std::ranges::find(values, name) != values.end();
}

struct nv_block
{
	std::vector<std::string> storage;
	std::vector<nghttp2_nv> values;
};

void add_nv(nv_block &block, std::string name, std::string value)
{
	block.storage.push_back(std::move(name));
	block.storage.push_back(std::move(value));
	auto &stored_name = block.storage[block.storage.size() - 2];
	auto &stored_value = block.storage.back();
	block.values.push_back({
		reinterpret_cast<uint8_t *>(stored_name.data()),
		reinterpret_cast<uint8_t *>(stored_value.data()),
		stored_name.size(), stored_value.size(), NGHTTP2_NV_FLAG_NONE
	});
}

[[nodiscard]] sys_expected<nv_block> response_headers(
	const response_head &head, const body_spec &spec, size_t limit) noexcept
{
	try {
		const auto code = head.status.code();
		const bool body_allowed = head.status.allows_body();
		if( not body_allowed and spec.type() != body_spec::mode::empty )
			return sys_unexpected(make_error_code(errc::body_not_allowed));
		nv_block result;
		result.storage.reserve((head.headers.size() + 2) * 2);
		result.values.reserve(head.headers.size() + 2);
		add_nv(result, ":status", std::to_string(code));
		size_t decoded_size = 32 + std::string_view(":status").size() + 3;
		if( decoded_size > limit )
			return sys_unexpected(
				make_error_code(errc::header_limit_exceeded));
		for( const auto &field : head.headers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden_header(name) )
				return sys_unexpected(make_error_code(errc::invalid_field_name));
			if( name == "content-length" or name == "trailer" ) continue;
			if( name == "te" and lower(field.value.to_string()) != "trailers" )
				return sys_unexpected(make_error_code(errc::invalid_field_value));
			if( name.size() + field.value->size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			decoded_size += name.size() + field.value->size() + 32;
			add_nv(result, std::move(name), field.value.to_string());
		}
		if( body_allowed and (spec.type() == body_spec::mode::known_length or
			spec.type() == body_spec::mode::head) )
		{
			auto length = std::to_string(*spec.length());
			if( std::string_view("content-length").size() + length.size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			add_nv(result, "content-length", std::move(length));
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] sys_expected<nv_block> trailer_headers(
	const fields &trailers, size_t limit) noexcept
{
	try {
		nv_block result;
		result.storage.reserve(trailers.size() * 2);
		result.values.reserve(trailers.size());
		size_t decoded_size = 0;
		for( const auto &field : trailers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden_header(name) or name == "te" or
				name == "content-length" or name == "host" or name == "trailer" )
				return sys_unexpected(make_error_code(errc::invalid_trailer));
			if( name.size() + field.value->size() + 32 >
				limit - std::min(limit, decoded_size) )
				return sys_unexpected(
					make_error_code(errc::header_limit_exceeded));
			decoded_size += name.size() + field.value->size() + 32;
			add_nv(result, std::move(name), field.value.to_string());
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

class server_reader_adapter final : public body_reader_adapter
{
public:
	explicit server_reader_adapter(std::weak_ptr<server_stream_impl> owner) :
		m_owner(std::move(owner)) {}

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	void read_some(mutable_buffer buffer, read_handler_t handler) override;
	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override;
	void trailers(trailers_handler_t handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<server_stream_impl> m_owner;
};

class server_writer_adapter final : public body_writer_adapter
{
public:
	explicit server_writer_adapter(std::weak_ptr<server_stream_impl> owner) :
		m_owner(std::move(owner)) {}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override;
	void write(const_buffer buffer, write_handler_t handler) override;
	void finish_sync(fields trailers, error_code &error) noexcept override;
	void finish(fields trailers, finish_handler_t handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<server_stream_impl> m_owner;
};

class server_stream_impl final : public stream,
	public std::enable_shared_from_this<server_stream_impl>
{
public:
	server_stream_impl(std::shared_ptr<server_session_impl> session,
		int32_t id) : m_session(std::move(session)), m_id(id) {}
	~server_stream_impl() override;

	void initialize();
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return {.trailers = true, .full_duplex = true,
			.tunnel = m_duplex_opened,
			.extended_connect = m_request.connect_protocol.has_value()};
	}
	[[nodiscard]] request_head request() const override { return m_request; }
	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override { return m_inbound; }
	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override { return m_outbound; }
	[[nodiscard]] endpoint_t remote_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] endpoint_t local_endpoint(
		error_code &error) const noexcept override;
	void respond_sync(response_head head, body_spec spec,
		error_code &error) noexcept override;
	void respond(response_head head, body_spec spec,
		operation_handler_t handler) override;
	void inform_sync(response_head head, error_code &error) noexcept override;
	void inform(response_head head, operation_handler_t handler) override;
	[[nodiscard]] std::shared_ptr<duplex_state> open_duplex_sync(
		optional<response_head> response, error_code &error) noexcept override;
	void open_duplex(optional<response_head> response,
		duplex_handler_t handler) override;
	[[nodiscard]] bool reusable() const noexcept override;
	void cancel() noexcept override;

	std::shared_ptr<server_session_impl> m_session;
	int32_t m_id;
	request_head m_request;
	std::string m_method;
	std::string m_scheme;
	std::string m_authority;
	std::string m_path;
	std::string m_protocol;
	bool m_have_method = false;
	bool m_have_scheme = false;
	bool m_have_authority = false;
	bool m_have_path = false;
	bool m_have_protocol = false;
	bool m_collecting_trailers = false;
	bool m_request_ready = false;
	bool m_invalid_headers = false;
	size_t m_header_bytes = 0;
	size_t m_header_count = 0;
	optional<uint64_t> m_content_length;

	std::shared_ptr<server_reader_adapter> m_reader_adapter;
	std::shared_ptr<server_writer_adapter> m_writer_adapter;
	std::shared_ptr<body_reader_state> m_inbound;
	std::shared_ptr<body_writer_state> m_outbound;

	std::deque<std::vector<std::byte>> m_inbound_chunks;
	size_t m_inbound_offset = 0;
	size_t m_inbound_size = 0;
	uint64_t m_inbound_total = 0;
	bool m_remote_end = false;
	fields m_inbound_trailers;
	optional<mutable_buffer> m_pending_read_buffer;
	body_reader_adapter::read_handler_t m_pending_read_handler;

	std::deque<std::vector<std::byte>> m_outbound_chunks;
	size_t m_outbound_offset = 0;
	size_t m_outbound_size = 0;
	bool m_response_started = false;
	bool m_outbound_finished = false;
	bool m_trailers_submitted = false;
	fields m_outbound_trailers;
	struct pending_write
	{
		const_buffer buffer;
		body_writer_adapter::write_handler_t handler;
	};
	optional<pending_write> m_pending_write;

	error_code m_terminal_error;
	bool m_closed = false;
	bool m_cancelled = false;
	bool m_duplex_opened = false;
};

class server_session_impl final : public server_session,
	public std::enable_shared_from_this<server_session_impl>
{
public:
	server_session_impl(
		std::shared_ptr<::riwo::http_exp::connection> transport,
		server_config config, std::string initial_input) :
		m_executor(transport->get_executor()), m_strand(asio::make_strand(m_executor)),
		m_transport(std::move(transport)), m_config(std::move(config)),
		m_initial_input(std::move(initial_input))
	{
		error_code error;
		m_remote = m_transport->remote_endpoint(error);
		error.clear();
		m_local = m_transport->local_endpoint(error);
	}

	~server_session_impl() override
	{
		if( m_native ) nghttp2_session_del(m_native);
		if( m_callbacks ) nghttp2_session_callbacks_del(m_callbacks);
		if( m_option ) nghttp2_option_del(m_option);
	}

	[[nodiscard]] error_code initialize() noexcept
	{
		if( nghttp2_session_callbacks_new(&m_callbacks) != 0 or
			nghttp2_option_new(&m_option) != 0 )
			return make_error_code(errc::operation_not_supported);
		nghttp2_session_callbacks_set_on_begin_headers_callback(m_callbacks,
			&server_session_impl::on_begin_headers);
		nghttp2_session_callbacks_set_on_header_callback(m_callbacks,
			&server_session_impl::on_header);
		nghttp2_session_callbacks_set_on_frame_recv_callback(m_callbacks,
			&server_session_impl::on_frame_recv);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(m_callbacks,
			&server_session_impl::on_data_chunk);
		nghttp2_session_callbacks_set_on_stream_close_callback(m_callbacks,
			&server_session_impl::on_stream_close);
		nghttp2_option_set_no_auto_window_update(m_option, 1);
		nghttp2_option_set_max_deflate_dynamic_table_size(m_option,
			m_config.h2.max_dynamic_table_size);
		if( nghttp2_session_server_new2(&m_native, m_callbacks, this, m_option) != 0 )
			return make_error_code(errc::operation_not_supported);

		std::array<nghttp2_settings_entry,6> settings {{
			{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
				m_config.h2.initial_stream_window},
			{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
				m_config.h2.max_concurrent_streams},
			{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, m_config.h2.max_frame_size},
			{NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
				static_cast<uint32_t>(std::min<size_t>(
					m_config.h2.max_dynamic_table_size, UINT32_MAX))},
			{NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
				static_cast<uint32_t>(std::min(
					m_config.h2.max_header_list_size,
					m_config.request_limits.max_header_list_size))},
			{NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL,
				m_config.h2.enable_extended_connect ? 1U : 0U}
		}};
		if( nghttp2_submit_settings(m_native, NGHTTP2_FLAG_NONE,
			settings.data(), settings.size()) != 0 ) return protocol_error();
		if( m_config.h2.initial_connection_window > 65'535 )
		{
			const auto increment = static_cast<int32_t>(
				m_config.h2.initial_connection_window - 65'535);
			if( nghttp2_submit_window_update(m_native, NGHTTP2_FLAG_NONE,
				0, increment) != 0 ) return protocol_error();
		}
		if( not m_initial_input.empty() )
		{
			const auto consumed = nghttp2_api::session_mem_recv(m_native,
				reinterpret_cast<const uint8_t *>(m_initial_input.data()),
				m_initial_input.size());
			if( consumed < 0 or static_cast<size_t>(consumed) !=
				m_initial_input.size() ) return protocol_error();
			m_initial_input.clear();
		}
		m_transport->set_read_timeout(m_config.timeout.body_idle);
		m_transport->set_write_timeout(m_config.timeout.body_idle);
		return {};
	}

	void start()
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self)] {
			self->write_pump();
			self->read_loop();
		});
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return m_executor;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] bool draining() const noexcept override { return m_draining; }
	[[nodiscard]] bool closed() const noexcept override { return m_closed; }

	void accept(accept_handler_t handler) override
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self),
			handler = std::move(handler)]() mutable {
			if( self->m_accept_handler )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress),
					std::shared_ptr<stream>{});
			if( not self->m_ready_streams.empty() )
			{
				auto value = std::move(self->m_ready_streams.front());
				self->m_ready_streams.pop_front();
				return std::move(handler)(error_code{}, std::move(value));
			}
			if( self->m_closed or self->m_draining )
				return std::move(handler)(aborted(), std::shared_ptr<stream>{});
			self->m_accept_handler = std::move(handler);
		});
	}

	void graceful_shutdown(shutdown_handler_t handler) override
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self),
			handler = std::move(handler)]() mutable {
			if( self->m_closed )
				return std::move(handler)(error_code{});
			self->m_draining = true;
			(void)nghttp2_submit_goaway(self->m_native, NGHTTP2_FLAG_NONE,
				self->m_last_processed_stream, NGHTTP2_NO_ERROR, nullptr, 0);
			self->m_shutdown_handlers.push_back(std::move(handler));
			if( self->m_accept_handler )
				std::move(self->m_accept_handler)(aborted(),
					std::shared_ptr<stream>{});
			self->write_pump();
			self->finish_shutdown_if_idle();
		});
	}

	[[nodiscard]] const server_config &config() const noexcept { return m_config; }
	[[nodiscard]] asio::ip::tcp::endpoint remote_endpoint(
		error_code &error) const noexcept { error.clear(); return m_remote; }
	[[nodiscard]] asio::ip::tcp::endpoint local_endpoint(
		error_code &error) const noexcept { error.clear(); return m_local; }

	[[nodiscard]] body_read_result read_sync(server_stream_impl &value,
		mutable_buffer buffer, error_code &error) noexcept
	{
		error.clear();
		if( value.m_terminal_error ) { error = value.m_terminal_error; return {}; }
		if( value.m_inbound_size == 0 and not value.m_remote_end )
		{
			error = make_error_code(errc::operation_in_progress);
			return {};
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		return result;
	}

	void read(const std::shared_ptr<server_stream_impl> &value,
		mutable_buffer buffer, body_reader_adapter::read_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value, buffer,
			handler = std::move(handler)]() mutable {
			if( value->m_terminal_error )
				return std::move(handler)(value->m_terminal_error,
					body_read_result{});
			if( value->m_pending_read_handler )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress),
					body_read_result{});
			if( value->m_inbound_size != 0 or value->m_remote_end )
			{
				auto result = self->take_body(*value, buffer);
				if( result.bytes != 0 ) self->consume(*value, result.bytes);
				return std::move(handler)(error_code{}, result);
			}
			value->m_pending_read_buffer = buffer;
			value->m_pending_read_handler = std::move(handler);
		});
	}

	[[nodiscard]] fields trailers_sync(server_stream_impl &value,
		error_code &error) noexcept
	{
		error.clear();
		if( not value.m_remote_end )
			error = make_error_code(errc::operation_not_permitted);
		return error ? fields{} : value.m_inbound_trailers;
	}

	void trailers(const std::shared_ptr<server_stream_impl> &value,
		body_reader_adapter::trailers_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value,
			handler = std::move(handler)]() mutable {
			ignore_unused(self);
			if( not value->m_remote_end )
				std::move(handler)(make_error_code(
					errc::operation_not_permitted), fields{});
			else std::move(handler)(error_code{}, value->m_inbound_trailers);
		});
	}

	void respond_sync(server_stream_impl &value, response_head head,
		body_spec spec, error_code &error) noexcept
	{
		error.clear();
		if( value.m_response_started or value.m_closed )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		if( spec.type() == body_spec::mode::head and
			value.m_request.method.known() != method::head )
		{
			error = make_error_code(errc::operation_not_permitted);
			return;
		}
		submit_response(value, std::move(head), std::move(spec), error);
		if( not error ) write_pump();
	}

	void respond(const std::shared_ptr<server_stream_impl> &value,
		response_head head, body_spec spec, stream::operation_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value,
			head = std::move(head), spec = std::move(spec),
			handler = std::move(handler)]() mutable {
			if( value->m_response_started or value->m_closed )
				return std::move(handler)(
					make_error_code(errc::operation_not_permitted));
			if( spec.type() == body_spec::mode::head and
				value->m_request.method.known() != method::head )
				return std::move(handler)(
					make_error_code(errc::operation_not_permitted));
			error_code error;
			self->submit_response(*value, std::move(head), std::move(spec), error);
			if( not error ) self->write_pump();
			std::move(handler)(error);
		});
	}

	void inform_sync(server_stream_impl &value, response_head head,
		error_code &error) noexcept
	{
		error.clear();
		auto headers = response_headers(head, body_spec::empty(),
			m_config.h2.max_header_list_size);
		if( not headers ) { error = headers.error(); return; }
		if( nghttp2_submit_headers(m_native, NGHTTP2_FLAG_NONE, value.m_id, nullptr,
			headers->values.data(), headers->values.size(), nullptr) != 0 )
			error = make_error_code(errc::stream_protocol_error);
		if( not error ) write_pump();
	}

	void inform(const std::shared_ptr<server_stream_impl> &value,
		response_head head, stream::operation_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value,
			head = std::move(head), handler = std::move(handler)]() mutable {
			error_code error;
			self->inform_sync(*value, std::move(head), error);
			std::move(handler)(error);
		});
	}

	[[nodiscard]] size_t write_sync(server_stream_impl &value,
		const_buffer buffer, error_code &error) noexcept
	{
		error.clear();
		if( value.m_terminal_error ) { error = value.m_terminal_error; return 0; }
		// A HEAD response carries the representation metadata but no body on
		// the wire.  HTTP/1.x enforces this in its writer adapter; the h2
		// session must discard the same writes before they reach nghttp2.
		if( value.m_request.method.known() == method::head )
			return buffer.size();
		if( buffer.size() > m_config.h2.outbound_stream_buffer or
			value.m_outbound_size + buffer.size() >
				m_config.h2.outbound_stream_buffer or
			m_outbound_buffered + buffer.size() >
				m_config.h2.outbound_connection_buffer )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			return 0;
		}
		enqueue(value, buffer);
		(void)nghttp2_session_resume_data(m_native, value.m_id);
		write_pump();
		return buffer.size();
	}

	void write(const std::shared_ptr<server_stream_impl> &value,
		const_buffer buffer, body_writer_adapter::write_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value, buffer,
			handler = std::move(handler)]() mutable {
			if( value->m_terminal_error )
				return std::move(handler)(value->m_terminal_error, 0);
			if( value->m_request.method.known() == method::head )
				return std::move(handler)(error_code{}, buffer.size());
			if( value->m_pending_write )
				return std::move(handler)(
					make_error_code(errc::operation_in_progress), 0);
			if( buffer.size() > self->m_config.h2.outbound_stream_buffer )
				return std::move(handler)(
					make_error_code(errc::buffer_limit_exceeded), 0);
			if( value->m_outbound_size + buffer.size() >
					self->m_config.h2.outbound_stream_buffer or
				self->m_outbound_buffered + buffer.size() >
					self->m_config.h2.outbound_connection_buffer )
			{
				value->m_pending_write.emplace(
					server_stream_impl::pending_write{
						buffer, std::move(handler)});
				return;
			}
			const auto size = buffer.size();
			self->enqueue(*value, buffer);
			(void)nghttp2_session_resume_data(self->m_native, value->m_id);
			self->write_pump();
			std::move(handler)(error_code{}, size);
		});
	}

	void finish_sync(server_stream_impl &value, fields trailers,
		error_code &error) noexcept
	{
		error.clear();
		if( value.m_terminal_error ) { error = value.m_terminal_error; return; }
		if( value.m_request.method.known() == method::head )
			return;
		auto checked = trailer_headers(trailers,
			m_config.h2.max_header_list_size);
		if( not checked ) { error = checked.error(); return; }
		value.m_outbound_finished = true;
		value.m_outbound_trailers = std::move(trailers);
		(void)nghttp2_session_resume_data(m_native, value.m_id);
		write_pump();
	}

	void finish(const std::shared_ptr<server_stream_impl> &value,
		fields trailers, body_writer_adapter::finish_handler_t handler)
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value,
			trailers = std::move(trailers), handler = std::move(handler)]() mutable {
			if( value->m_terminal_error )
				return std::move(handler)(value->m_terminal_error);
			if( value->m_request.method.known() == method::head )
				return std::move(handler)(error_code{});
			auto checked = trailer_headers(trailers,
				self->m_config.h2.max_header_list_size);
			if( not checked ) return std::move(handler)(checked.error());
			value->m_outbound_finished = true;
			value->m_outbound_trailers = std::move(trailers);
			(void)nghttp2_session_resume_data(self->m_native, value->m_id);
			self->write_pump();
			std::move(handler)(error_code{});
		});
	}

	void cancel(const std::shared_ptr<server_stream_impl> &value) noexcept
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), value] {
			self->cancel_stream(*value);
			self->write_pump();
		});
	}

	void release_buffer_budget(size_t inbound, size_t outbound) noexcept
	{
		auto self = shared_from_this();
		asio::post(m_strand, [self = std::move(self), inbound, outbound] {
			self->m_inbound_buffered -= std::min(
				self->m_inbound_buffered, inbound);
			self->m_outbound_buffered -= std::min(
				self->m_outbound_buffered, outbound);
		});
	}

private:
	void submit_response(server_stream_impl &value, response_head head,
		body_spec spec, error_code &error) noexcept
	{
		auto headers = response_headers(head, spec,
			m_config.h2.max_header_list_size);
		if( not headers ) { error = headers.error(); return; }
		nghttp2_api::data_provider_t provider {};
		nghttp2_api::data_provider_t *provider_ptr = nullptr;
		const auto mode = spec.type();
		const bool head_request =
			value.m_request.method.known() == method::head;
		if( mode != body_spec::mode::empty and mode != body_spec::mode::head and
			not head_request )
		{
			provider.source.ptr = &value;
			provider.read_callback = &server_session_impl::data_read;
			provider_ptr = &provider;
		}
		if( nghttp2_api::submit_response(m_native, value.m_id,
			headers->values.data(), headers->values.size(), provider_ptr) != 0 )
		{
			error = make_error_code(errc::stream_protocol_error);
			return;
		}
		value.m_response_started = true;
		value.m_outbound_finished =
			mode == body_spec::mode::empty or mode == body_spec::mode::head or
			head_request;
	}

	void read_loop()
	{
		if( m_reading or m_closed ) return;
		m_reading = true;
		auto self = shared_from_this();
		m_transport->read_some(riwo::buffer(m_read_buffer),
			asio::bind_executor(m_strand,
			[self = std::move(self)](error_code error, size_t size) mutable {
				self->m_reading = false;
				if( error or size == 0 )
					return self->fail(error ? error : protocol_error());
				const auto consumed = nghttp2_api::session_mem_recv(
					self->m_native, reinterpret_cast<const uint8_t *>(
						self->m_read_buffer.data()), size);
				if( consumed < 0 or static_cast<size_t>(consumed) != size )
					return self->fail(protocol_error());
				self->write_pump();
				self->read_loop();
			}));
	}

	void write_pump()
	{
		if( m_writing or m_closed ) return;
		const uint8_t *data = nullptr;
		const auto size = nghttp2_api::session_mem_send(m_native, &data);
		if( size < 0 ) return fail(protocol_error());
		if( size == 0 ) return;
		if( static_cast<size_t>(size) >
			m_config.h2.outbound_connection_buffer )
			return fail(make_error_code(errc::buffer_limit_exceeded));
		m_writing = true;
		auto wire = std::make_shared<std::vector<std::byte>>(
			static_cast<size_t>(size));
		std::memcpy(wire->data(), data, wire->size());
		auto self = shared_from_this();
		m_transport->write(riwo::buffer(*wire), asio::bind_executor(m_strand,
			[self = std::move(self), wire]
			(error_code error, size_t written) mutable {
				self->m_writing = false;
				if( error or written != wire->size() )
					return self->fail(error ? error : protocol_error());
				self->write_pump();
			}));
	}

	void fail(error_code error)
	{
		if( m_closed.exchange(true) ) return;
		m_draining = true;
		for( auto &[id, value] : m_streams )
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		m_streams.clear();
		m_ready_streams.clear();
		if( m_accept_handler )
			std::move(m_accept_handler)(error, std::shared_ptr<stream>{});
		m_transport->cancel();
		complete_shutdown(error);
	}

	void complete_error(server_stream_impl &value, error_code error)
	{
		if( not value.m_terminal_error ) value.m_terminal_error = error;
		if( value.m_pending_read_handler )
		{
			value.m_pending_read_buffer.reset();
			std::move(value.m_pending_read_handler)(value.m_terminal_error,
				body_read_result{});
		}
		if( value.m_pending_write )
		{
			auto pending = std::move(*value.m_pending_write);
			value.m_pending_write.reset();
			std::move(pending.handler)(value.m_terminal_error, 0);
		}
	}

	void complete_shutdown(error_code error)
	{
		auto handlers = std::move(m_shutdown_handlers);
		for( auto &handler : handlers ) std::move(handler)(error);
	}

	void finish_shutdown_if_idle()
	{
		if( not m_draining or not m_streams.empty() or m_closed ) return;
		m_closed = true;
		m_transport->close(
			[self = shared_from_this()](error_code error) mutable {
				self->complete_shutdown(error);
			});
	}

	[[nodiscard]] body_read_result take_body(server_stream_impl &value,
		mutable_buffer buffer)
	{
		size_t copied = 0;
		while( copied < buffer.size() and not value.m_inbound_chunks.empty() )
		{
			auto &chunk = value.m_inbound_chunks.front();
			const auto available = chunk.size() - value.m_inbound_offset;
			const auto count = std::min(available, buffer.size() - copied);
			std::memcpy(static_cast<std::byte *>(buffer.data()) + copied,
				chunk.data() + value.m_inbound_offset, count);
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
		return {copied, value.m_remote_end and value.m_inbound_size == 0};
	}

	void consume(server_stream_impl &value, size_t size)
	{
		if( size == 0 or value.m_id <= 0 ) return;
		if( nghttp2_session_consume(m_native, value.m_id, size) != 0 )
			return fail(protocol_error());
		write_pump();
	}

	void enqueue(server_stream_impl &value, const_buffer buffer)
	{
		std::vector<std::byte> bytes(buffer.size());
		if( buffer.size() != 0 )
			std::memcpy(bytes.data(), buffer.data(), buffer.size());
		value.m_outbound_size += bytes.size();
		m_outbound_buffered += bytes.size();
		value.m_outbound_chunks.push_back(std::move(bytes));
	}

	void drain_pending(server_stream_impl &value)
	{
		if( not value.m_pending_write ) return;
		auto &pending = *value.m_pending_write;
		if( value.m_outbound_size + pending.buffer.size() >
				m_config.h2.outbound_stream_buffer or
			m_outbound_buffered + pending.buffer.size() >
				m_config.h2.outbound_connection_buffer ) return;
		auto item = std::move(pending);
		value.m_pending_write.reset();
		const auto size = item.buffer.size();
		enqueue(value, item.buffer);
		std::move(item.handler)(error_code{}, size);
	}

	void deliver_pending_read(server_stream_impl &value)
	{
		if( not value.m_pending_read_handler or
			(value.m_inbound_size == 0 and not value.m_remote_end and
				not value.m_terminal_error) ) return;
		auto handler = std::move(value.m_pending_read_handler);
		auto buffer = *value.m_pending_read_buffer;
		value.m_pending_read_buffer.reset();
		if( value.m_terminal_error )
			return std::move(handler)(value.m_terminal_error,
				body_read_result{});
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		std::move(handler)(error_code{}, result);
	}

	void remote_end(server_stream_impl &value)
	{
		value.m_remote_end = true;
		if( value.m_content_length and
			*value.m_content_length != value.m_inbound_total )
		{
			complete_error(value,
				make_error_code(errc::body_length_mismatch));
			(void)nghttp2_submit_rst_stream(m_native, NGHTTP2_FLAG_NONE,
				value.m_id, NGHTTP2_PROTOCOL_ERROR);
		}
		// A header-only request has no read operation through which
		// body_reader_state could observe EOF. Mark it complete here so the
		// public reader destructor does not cancel an otherwise successful
		// response stream.
		if( value.m_inbound_size == 0 and value.m_inbound )
			value.m_inbound->set_finished();
		deliver_pending_read(value);
	}

	void cancel_stream(server_stream_impl &value)
	{
		if( value.m_cancelled or value.m_closed ) return;
		value.m_cancelled = true;
		complete_error(value, aborted());
		(void)nghttp2_submit_rst_stream(m_native, NGHTTP2_FLAG_NONE,
			value.m_id, NGHTTP2_CANCEL);
	}

	void deliver_stream(std::shared_ptr<server_stream_impl> value)
	{
		m_last_processed_stream = std::max(m_last_processed_stream, value->m_id);
		if( m_accept_handler )
			std::move(m_accept_handler)(error_code{}, std::move(value));
		else m_ready_streams.push_back(std::move(value));
	}

	[[nodiscard]] bool finish_request(server_stream_impl &value)
	{
		if( not value.m_have_method or not value.m_have_scheme or
			not value.m_have_authority or not value.m_have_path or
			value.m_method.empty() or value.m_authority.empty() or
			(value.m_path != "*" and not value.m_path.starts_with('/')) )
			return false;
		if( (value.m_scheme != "http" and value.m_scheme != "https") or
			std::ranges::any_of(value.m_authority, [](unsigned char byte) {
				return byte <= 0x20U or byte == 0x7fU or byte == '/' or
					byte == '?' or byte == '#' or byte == '@';
			}) or std::ranges::any_of(value.m_path, [](unsigned char byte) {
				return byte <= 0x20U or byte == 0x7fU or byte == '#';
			}) ) return false;
		try {
			value.m_request.method = http_exp::method(value.m_method);
			value.m_request.scheme = value.m_scheme;
			value.m_request.authority = value.m_authority;
			value.m_request.target = value.m_path;
			if( value.m_have_protocol )
			{
				if( not m_config.h2.enable_extended_connect or
					value.m_method != "CONNECT" or value.m_protocol.empty() )
					return false;
				value.m_request.connect_protocol = value.m_protocol;
			}
			if( value.m_path != "*" )
				value.m_request.url = riwo::url(value.m_scheme + "://" +
					value.m_authority + value.m_path);
			optional<uint64_t> content_length_value;
			for( const auto &field : value.m_request.headers )
			{
				if( lower(field.name) != "content-length" ) continue;
				const std::string_view content_length = field.value.to_string();
				uint64_t length = 0;
				const auto parsed = std::from_chars(content_length.data(),
					content_length.data() + content_length.size(), length);
				if( parsed.ec != std::errc{} or
					parsed.ptr != content_length.data() + content_length.size() or
					(content_length_value and
						*content_length_value != length) ) return false;
				content_length_value = length;
			}
			value.m_content_length = content_length_value;
			if( value.m_content_length and
				m_config.request_limits.max_body_size and
				*value.m_content_length >
					*m_config.request_limits.max_body_size ) return false;
			value.m_request_ready = true;
			return true;
		}
		catch(...) { return false; }
	}

	static int on_begin_headers(nghttp2_session *,
		const nghttp2_frame *frame, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		if( frame->hd.type != NGHTTP2_HEADERS ) return 0;
		if( frame->headers.cat == NGHTTP2_HCAT_REQUEST )
		{
			if( self.m_draining or self.m_streams.size() >=
				self.m_config.h2.max_concurrent_streams )
			{
				(void)nghttp2_submit_rst_stream(self.m_native, NGHTTP2_FLAG_NONE,
					frame->hd.stream_id, NGHTTP2_REFUSED_STREAM);
				return 0;
			}
			try {
				auto value = std::make_shared<server_stream_impl>(
					self.shared_from_this(), frame->hd.stream_id);
				value->initialize();
				self.m_streams.emplace(frame->hd.stream_id, value);
				(void)nghttp2_session_set_stream_user_data(self.m_native,
					frame->hd.stream_id, value.get());
			}
			catch(...) { return NGHTTP2_ERR_CALLBACK_FAILURE; }
			return 0;
		}
		auto found = self.m_streams.find(frame->hd.stream_id);
		if( found != self.m_streams.end() )
		{
			found->second->m_collecting_trailers = true;
			found->second->m_header_bytes = 0;
			found->second->m_header_count = 0;
		}
		return 0;
	}

	static int on_header(nghttp2_session *, const nghttp2_frame *frame,
		const uint8_t *name, size_t name_size, const uint8_t *field_value,
		size_t value_size, uint8_t, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.m_streams.find(frame->hd.stream_id);
		if( found == self.m_streams.end() ) return 0;
		auto &value = *found->second;
		const auto header_limit = std::min(self.m_config.h2.max_header_list_size,
			self.m_config.request_limits.max_header_list_size);
		value.m_header_bytes += name_size + value_size + 32;
		++value.m_header_count;
		if( value.m_header_bytes > header_limit or
			value.m_header_count > self.m_config.request_limits.max_field_count or
			name_size > self.m_config.request_limits.max_field_name_size or
			value_size > self.m_config.request_limits.max_field_value_size )
		{
			value.m_invalid_headers = true;
			(void)nghttp2_submit_rst_stream(self.m_native, NGHTTP2_FLAG_NONE,
				value.m_id, NGHTTP2_ENHANCE_YOUR_CALM);
			return 0;
		}
		const std::string_view header_name(
			reinterpret_cast<const char *>(name), name_size);
		const std::string_view header_value(
			reinterpret_cast<const char *>(field_value), value_size);
		if( header_name.starts_with(':') )
		{
			if( value.m_collecting_trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			auto set_once = [&](std::string &target, bool &present) {
				if( present ) return false;
				target.assign(header_value);
				present = true;
				return true;
			};
			if( header_name == ":method" )
				return set_once(value.m_method, value.m_have_method) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":scheme" )
				return set_once(value.m_scheme, value.m_have_scheme) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":authority" )
				return set_once(value.m_authority, value.m_have_authority) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":path" )
				return set_once(value.m_path, value.m_have_path) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			if( header_name == ":protocol" )
				return set_once(value.m_protocol, value.m_have_protocol) ? 0 :
					NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		}
		if( forbidden_header(header_name) or
			(header_name == "te" and lower(header_value) != "trailers") or
			(value.m_collecting_trailers and
				(header_name == "content-length" or header_name == "host" or
					header_name == "trailer" or header_name == "te")) )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		try {
			if( value.m_collecting_trailers )
				value.m_inbound_trailers.add(std::string(header_name),
					std::string(header_value));
			else value.m_request.headers.add(std::string(header_name),
				std::string(header_value));
		}
		catch(...) { return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_frame_recv(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		if( frame->hd.type == NGHTTP2_GOAWAY )
		{
			self.m_draining = true;
			return 0;
		}
		if( frame->hd.type != NGHTTP2_HEADERS and
			frame->hd.type != NGHTTP2_DATA and
			frame->hd.type != NGHTTP2_RST_STREAM ) return 0;
		auto found = self.m_streams.find(frame->hd.stream_id);
		if( found == self.m_streams.end() ) return 0;
		auto value = found->second;
		const bool end_stream =
			(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;
		if( frame->hd.type == NGHTTP2_HEADERS and not value->m_request_ready and
			frame->headers.cat == NGHTTP2_HCAT_REQUEST )
		{
			if( value->m_invalid_headers or not self.finish_request(*value) )
			{
				(void)nghttp2_submit_rst_stream(self.m_native, NGHTTP2_FLAG_NONE,
					value->m_id, NGHTTP2_PROTOCOL_ERROR);
				self.complete_error(*value,
					make_error_code(errc::stream_protocol_error));
			}
			else
			{
				// Publish a header-only request only after its inbound direction is
				// observably complete. The service executor may run inline.
				if( end_stream ) self.remote_end(*value);
				self.deliver_stream(value);
			}
		}
		if( frame->hd.type == NGHTTP2_RST_STREAM )
			self.complete_error(*value,
				make_error_code(errc::stream_protocol_error));
		if( end_stream and not value->m_remote_end ) self.remote_end(*value);
		return 0;
	}

	static int on_data_chunk(nghttp2_session *, uint8_t, int32_t stream_id,
		const uint8_t *data, size_t size, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.m_streams.find(stream_id);
		if( found == self.m_streams.end() ) return 0;
		auto &value = *found->second;
		if( value.m_inbound_size + size >
				self.m_config.h2.inbound_stream_buffer or
			self.m_inbound_buffered + size >
				self.m_config.h2.inbound_connection_buffer or
			(self.m_config.request_limits.max_body_size and
				value.m_inbound_total + size >
					*self.m_config.request_limits.max_body_size) )
		{
			(void)nghttp2_submit_rst_stream(self.m_native, NGHTTP2_FLAG_NONE,
				stream_id, NGHTTP2_ENHANCE_YOUR_CALM);
			self.complete_error(value,
				make_error_code(errc::buffer_limit_exceeded));
			return 0;
		}
		try {
			std::vector<std::byte> bytes(size);
			if( size != 0 ) std::memcpy(bytes.data(), data, size);
			value.m_inbound_chunks.push_back(std::move(bytes));
			value.m_inbound_size += size;
			value.m_inbound_total += size;
			self.m_inbound_buffered += size;
			self.deliver_pending_read(value);
		}
		catch(...) { return NGHTTP2_ERR_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_stream_close(nghttp2_session *, int32_t stream_id,
		uint32_t error_value, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto found = self.m_streams.find(stream_id);
		if( found == self.m_streams.end() ) return 0;
		auto value = found->second;
		value->m_closed = true;
		if( error_value != NGHTTP2_NO_ERROR and not value->m_terminal_error )
			self.complete_error(*value,
				make_error_code(errc::stream_protocol_error));
		if( error_value == NGHTTP2_NO_ERROR and not value->m_remote_end )
			self.remote_end(*value);
		self.m_streams.erase(found);
		self.finish_shutdown_if_idle();
		return 0;
	}

	static nghttp2_api::ssize_type_t data_read(nghttp2_session *session,
		int32_t, uint8_t *buffer, size_t length, uint32_t *flags,
		nghttp2_data_source *source, void *user_data)
	{
		auto &self = *static_cast<server_session_impl *>(user_data);
		auto &value = *static_cast<server_stream_impl *>(source->ptr);
		if( value.m_terminal_error ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		if( value.m_outbound_size == 0 )
		{
			if( not value.m_outbound_finished ) return NGHTTP2_ERR_DEFERRED;
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.m_outbound_trailers.empty() and
				not value.m_trailers_submitted )
			{
				auto trailers = trailer_headers(value.m_outbound_trailers,
					self.m_config.h2.max_header_list_size);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
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
			std::memcpy(buffer + copied,
				chunk.data() + value.m_outbound_offset, count);
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
			if( not value.m_outbound_trailers.empty() and
				not value.m_trailers_submitted )
			{
				auto trailers = trailer_headers(value.m_outbound_trailers,
					self.m_config.h2.max_header_list_size);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.m_id,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.m_trailers_submitted = true;
			}
		}
		return static_cast<nghttp2_api::ssize_type_t>(copied);
	}

	executor_type m_executor;
	asio::strand<asio::any_io_executor> m_strand;
	std::shared_ptr<::riwo::http_exp::connection> m_transport;
	server_config m_config;
	std::string m_initial_input;
	asio::ip::tcp::endpoint m_remote;
	asio::ip::tcp::endpoint m_local;
	nghttp2_session_callbacks *m_callbacks = nullptr;
	nghttp2_option *m_option = nullptr;
	nghttp2_session *m_native = nullptr;
	std::unordered_map<int32_t,std::shared_ptr<server_stream_impl>> m_streams;
	std::deque<std::shared_ptr<server_stream_impl>> m_ready_streams;
	accept_handler_t m_accept_handler;
	std::vector<shutdown_handler_t> m_shutdown_handlers;
	std::array<std::byte,16 * 1024> m_read_buffer {};
	size_t m_inbound_buffered = 0;
	size_t m_outbound_buffered = 0;
	int32_t m_last_processed_stream = 0;
	std::atomic<bool> m_draining {false};
	std::atomic<bool> m_closed {false};
	bool m_reading = false;
	bool m_writing = false;
};

server_stream_impl::~server_stream_impl()
{
	if( m_session )
		m_session->release_buffer_budget(m_inbound_size, m_outbound_size);
}

void server_stream_impl::initialize()
{
	m_reader_adapter = std::make_shared<server_reader_adapter>(weak_from_this());
	m_writer_adapter = std::make_shared<server_writer_adapter>(weak_from_this());
	m_inbound = make_body_reader_state(m_reader_adapter, true,
		m_session->config().request_limits.max_discard_body);
	m_outbound = make_body_writer_state(m_writer_adapter);
}

auto server_stream_impl::remote_endpoint(error_code &error) const noexcept
	-> endpoint_t { return m_session->remote_endpoint(error); }

auto server_stream_impl::local_endpoint(error_code &error) const noexcept
	-> endpoint_t { return m_session->local_endpoint(error); }

void server_stream_impl::respond_sync(response_head head, body_spec spec,
	error_code &error) noexcept {
	m_session->respond_sync(*this, std::move(head), std::move(spec), error);
}

void server_stream_impl::respond(response_head head, body_spec spec,
	operation_handler_t handler) {
	m_session->respond(shared_from_this(), std::move(head), std::move(spec),
		std::move(handler));
}

void server_stream_impl::inform_sync(response_head head,
	error_code &error) noexcept {
	m_session->inform_sync(*this, std::move(head), error);
}

void server_stream_impl::inform(response_head head,
	operation_handler_t handler) {
	m_session->inform(shared_from_this(), std::move(head), std::move(handler));
}

std::shared_ptr<duplex_state> server_stream_impl::open_duplex_sync(
	optional<response_head> response, error_code &error) noexcept
{
	error.clear();
	if( m_duplex_opened or not m_request.connect_protocol or not response or
		response->status.code() < 200 or response->status.code() >= 300 )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	respond_sync(std::move(*response), body_spec::streaming(), error);
	if( error ) return {};
	m_duplex_opened = true;
	error_code endpoint_error;
	auto remote = m_session->remote_endpoint(endpoint_error);
	endpoint_error.clear();
	auto local = m_session->local_endpoint(endpoint_error);
	auto self = shared_from_this();
	return make_body_duplex_state(m_session->get_executor(), protocol::v2,
		m_inbound, m_outbound, remote, local, self,
		[weak = weak_from_this()] {
			if( auto value = weak.lock() )
				value->m_session->cancel(value);
		});
}

void server_stream_impl::open_duplex(optional<response_head> response,
	duplex_handler_t handler)
{
	if( m_duplex_opened or not m_request.connect_protocol or not response or
		response->status.code() < 200 or response->status.code() >= 300 )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			std::shared_ptr<duplex_state>{});
		return;
	}
	auto self = shared_from_this();
	respond(std::move(*response), body_spec::streaming(),
	[self = std::move(self), handler = std::move(handler)]
	(error_code error) mutable {
		if( error )
		{
			std::move(handler)(error, std::shared_ptr<duplex_state>{});
			return;
		}
		self->m_duplex_opened = true;
		error_code endpoint_error;
		auto remote = self->m_session->remote_endpoint(endpoint_error);
		endpoint_error.clear();
		auto local = self->m_session->local_endpoint(endpoint_error);
		auto state = make_body_duplex_state(
			self->m_session->get_executor(), protocol::v2,
			self->m_inbound, self->m_outbound, remote, local, self,
			[weak = self->weak_from_this()] {
				if( auto value = weak.lock() )
					value->m_session->cancel(value);
			});
		std::move(handler)(error_code{}, std::move(state));
	});
}

bool server_stream_impl::reusable() const noexcept {
	// server_exchange_t destruction means the application handler is done, not
	// that nghttp2 has already emitted the final DATA/trailer frame.  Once both
	// application directions are complete, keep the stream alive so the Session
	// write pump can finish it instead of turning every successful response into
	// RST_STREAM(CANCEL).
	return not m_terminal_error and not m_cancelled and m_remote_end and
		m_response_started and m_outbound_finished;
}

void server_stream_impl::cancel() noexcept {
	if( m_duplex_opened ) return;
	if( m_session ) m_session->cancel(shared_from_this());
}

body_read_result server_reader_adapter::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	auto owner = m_owner.lock();
	if( not owner ) { error = aborted(); return {}; }
	return owner->m_session->read_sync(*owner, buffer, error);
}

void server_reader_adapter::read_some(mutable_buffer buffer,
	read_handler_t handler)
{
	auto owner = m_owner.lock();
	if( not owner ) return std::move(handler)(aborted(), body_read_result{});
	owner->m_session->read(owner, buffer, std::move(handler));
}

fields server_reader_adapter::trailers_sync(error_code &error) noexcept
{
	auto owner = m_owner.lock();
	if( not owner ) { error = aborted(); return {}; }
	return owner->m_session->trailers_sync(*owner, error);
}

void server_reader_adapter::trailers(trailers_handler_t handler)
{
	auto owner = m_owner.lock();
	if( not owner ) return std::move(handler)(aborted(), fields{});
	owner->m_session->trailers(owner, std::move(handler));
}

void server_reader_adapter::cancel() noexcept {
	if( auto owner = m_owner.lock() ) owner->cancel();
}

size_t server_writer_adapter::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	auto owner = m_owner.lock();
	if( not owner ) { error = aborted(); return 0; }
	return owner->m_session->write_sync(*owner, buffer, error);
}

void server_writer_adapter::write(const_buffer buffer, write_handler_t handler)
{
	auto owner = m_owner.lock();
	if( not owner ) return std::move(handler)(aborted(), 0);
	owner->m_session->write(owner, buffer, std::move(handler));
}

void server_writer_adapter::finish_sync(fields trailers,
	error_code &error) noexcept
{
	auto owner = m_owner.lock();
	if( not owner ) { error = aborted(); return; }
	owner->m_session->finish_sync(*owner, std::move(trailers), error);
}

void server_writer_adapter::finish(fields trailers, finish_handler_t handler)
{
	auto owner = m_owner.lock();
	if( not owner ) return std::move(handler)(aborted());
	owner->m_session->finish(owner, std::move(trailers), std::move(handler));
}

void server_writer_adapter::cancel() noexcept {
	if( auto owner = m_owner.lock() ) owner->cancel();
}

class v2_server_loop final : public std::enable_shared_from_this<v2_server_loop>
{
public:
	std::shared_ptr<server_session_impl> session;
	server_accept_handler_t handler;
	bool served = false;

	void start()
	{
		auto self = shared_from_this();
		session->accept(
		[self = std::move(self)](error_code error,
			std::shared_ptr<stream> value) mutable {
			if( error )
			{
				const auto benign =
					error == asio::error::make_error_code(asio::error::eof) or
					error == asio::error::make_error_code(
						asio::error::connection_reset) or
					error == asio::error::make_error_code(
						asio::error::operation_aborted);
				if( not self->served or not benign )
					self->handler(error, std::shared_ptr<stream>{},
						request_head{});
				return;
			}
			self->served = true;
			auto request = value ? value->request() : request_head{};
			self->handler(error_code{}, std::move(value), std::move(request));
			self->start();
		});
	}
};

class v2_server_connection final : public server_connection
{
public:
	explicit v2_server_connection(std::shared_ptr<v2_server_loop> loop) :
		m_loop(std::move(loop)) {}
	void cancel() noexcept override {
		m_loop->session->graceful_shutdown([](error_code) {});
	}
	[[nodiscard]] bool closed() const noexcept override {
		return m_loop->session->closed();
	}
private:
	std::shared_ptr<v2_server_loop> m_loop;
};

} //namespace

sys_expected<std::shared_ptr<server_session>> make_server_session(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input)
{
	if( not transport )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	if( auto error = validate_server_config(config) )
		return sys_unexpected(error);
	if( config.protocols.requirement == protocol_requirement::require_http1 or
		std::ranges::find(config.protocols.preference, protocol::v2) ==
			config.protocols.preference.end() )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	auto result = std::make_shared<server_session_impl>(std::move(transport),
		std::move(config), std::move(initial_input));
	if( auto error = result->initialize() ) return sys_unexpected(error);
	result->start();
	return std::static_pointer_cast<server_session>(std::move(result));
}

server_connection_ptr_t accept_h2(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input,
	server_accept_handler_t handler)
{
	auto made = make_server_session(std::move(transport), std::move(config),
		std::move(initial_input));
	if( not made )
	{
		std::move(handler)(made.error(), std::shared_ptr<stream>{},
			request_head{});
		return {};
	}
	auto concrete = std::static_pointer_cast<server_session_impl>(*made);
	auto loop = std::make_shared<v2_server_loop>();
	loop->session = std::move(concrete);
	loop->handler = std::move(handler);
	loop->start();
	return std::make_shared<v2_server_connection>(std::move(loop));
}

} //namespace riwo::http_exp::v2::detail

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
