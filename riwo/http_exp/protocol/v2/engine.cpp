// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "engine.h"

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

#include <riwo/http_exp/protocol/v2/detail/nghttp2_compat.h>
#include <charconv>
#include <deque>
#include <unordered_map>

namespace riwo::http_exp::v2 { namespace
{

namespace api = detail::nghttp2_api;

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] std::string lower(std::string_view value)
{
	std::string result(value);
	std::ranges::transform(result, result.begin(), [](unsigned char item) {
		return static_cast<char>(std::tolower(item));
	});
	return result;
}

[[nodiscard]] bool forbidden(std::string_view name) noexcept
{
	return name == "connection" or name == "proxy-connection" or
		name == "keep-alive" or name == "transfer-encoding" or
		name == "upgrade";
}

struct header_block
{
	std::vector<std::string> names;
	std::vector<std::string> values;
	std::vector<nghttp2_nv> native;

	void add(std::string name, std::string value)
	{
		names.push_back(std::move(name));
		values.push_back(std::move(value));
	}

	void finalize()
	{
		native.clear();
		native.reserve(names.size());
		for( size_t index = 0; index < names.size(); ++index )
			native.push_back(nghttp2_nv {
				.name = reinterpret_cast<uint8_t *>(names[index].data()),
				.value = reinterpret_cast<uint8_t *>(values[index].data()),
				.namelen = names[index].size(),
				.valuelen = values[index].size(),
				.flags = NGHTTP2_NV_FLAG_NONE
			});
	}
};

[[nodiscard]] sys_expected<header_block> request_headers(
	const request_head &head) noexcept
{
	try
	{
		header_block result;
		result.add(":method", std::string(head.method.text()));
		if( head.connect_protocol )
			result.add(":protocol", *head.connect_protocol);
		if( head.method.known() != method::connect or head.connect_protocol )
		{
			result.add(":scheme", head.scheme.empty() ?
				std::string(head.url.protocol()) : head.scheme);
			result.add(":path", head.target.empty() ? "/" : head.target);
		}
		result.add(":authority", head.authority);
		for( const auto &field : head.headers )
		{
			auto name = lower(field.name);
			if( name == "host" ) continue;
			if( forbidden(name) or (name == "te" and
				lower(field.value.to_string()) != "trailers") )
				return sys_unexpected(protocol_error());
			result.add(std::move(name), field.value.to_string());
		}
		result.finalize();
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] sys_expected<header_block> response_headers(
	const response_head &head) noexcept
{
	try
	{
		header_block result;
		result.add(":status", std::to_string(head.status.code()));
		for( const auto &field : head.headers )
		{
			auto name = lower(field.name);
			if( forbidden(name) or (name == "te" and
				lower(field.value.to_string()) != "trailers") )
				return sys_unexpected(protocol_error());
			result.add(std::move(name), field.value.to_string());
		}
		result.finalize();
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] sys_expected<header_block> trailer_headers(
	const fields &trailers) noexcept
{
	try
	{
		header_block result;
		for( const auto &field : trailers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden(name) or name == "te" or
				name == "content-length" or name == "trailer" )
				return sys_unexpected(make_error_code(errc::invalid_trailer));
			result.add(std::move(name), field.value.to_string());
		}
		result.finalize();
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

} //namespace

class connection_engine::impl
{
public:
	struct stream_state
	{
		impl *owner = nullptr;
		stream_token token = 0;
		int32_t wire_id = -1;
		std::deque<std::vector<std::byte>> outbound;
		size_t outbound_offset = 0;
		size_t outbound_size = 0;
		bool outbound_finished = false;
		bool provider = false;
		fields outbound_trailers;
		bool trailers_submitted = false;
		fields headers;
		std::string method;
		std::string scheme;
		std::string authority;
		std::string path;
		std::string protocol_name;
		optional<uint16_t> status_code;
		bool head_delivered = false;
		bool collecting_trailers = false;
		bool remote_end = false;
		size_t header_bytes = 0;
		size_t header_count = 0;
	};

	struct pending_event
	{
		protocol_event value;
		std::shared_ptr<std::vector<std::byte>> body;
	};

	explicit impl(connection_config value) : config(std::move(value)) {}
	~impl()
	{
		if( session ) nghttp2_session_del(session);
		if( callbacks ) nghttp2_session_callbacks_del(callbacks);
		if( option ) nghttp2_option_del(option);
	}

	[[nodiscard]] error_code initialize() noexcept
	{
		if( config.max_pending_output == 0 or
			config.max_concurrent_streams == 0 or
			config.max_frame_size < 16U * 1024U or
			config.max_frame_size > 16'777'215U )
			return make_error_code(errc::operation_not_permitted);
		if( nghttp2_session_callbacks_new(&callbacks) != 0 or
			nghttp2_option_new(&option) != 0 )
			return make_error_code(errc::operation_not_supported);
		nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
			&impl::on_begin_headers);
		nghttp2_session_callbacks_set_on_header_callback(callbacks,
			&impl::on_header);
		nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
			&impl::on_frame_recv);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
			&impl::on_data_chunk);
		nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
			&impl::on_stream_close);
		nghttp2_option_set_no_auto_window_update(option, 0);
		nghttp2_option_set_max_deflate_dynamic_table_size(option,
			config.max_dynamic_table_size);
		const int created = config.role == protocol_role::client ?
			nghttp2_session_client_new2(&session, callbacks, this, option) :
			nghttp2_session_server_new2(&session, callbacks, this, option);
		if( created != 0 ) return make_error_code(errc::operation_not_supported);
		std::array<nghttp2_settings_entry,5> settings {{
			{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, config.initial_stream_window},
			{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, config.max_concurrent_streams},
			{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, config.max_frame_size},
			{NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
				static_cast<uint32_t>(std::min<size_t>(
					config.max_dynamic_table_size, UINT32_MAX))},
			{NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL,
				config.enable_extended_connect ? 1U : 0U}
		}};
		if( nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE,
			settings.data(), settings.size()) != 0 )
			return protocol_error();
		if( config.initial_connection_window > 65'535 and
			nghttp2_submit_window_update(session, NGHTTP2_FLAG_NONE, 0,
				static_cast<int32_t>(config.initial_connection_window - 65'535)) != 0 )
			return protocol_error();
		return {};
	}

	[[nodiscard]] stream_state *by_wire(int32_t id) noexcept
	{
		auto found = wire_to_token.find(id);
		if( found == wire_to_token.end() ) return nullptr;
		auto value = streams.find(found->second);
		return value == streams.end() ? nullptr : value->second.get();
	}

	[[nodiscard]] stream_state &ensure_inbound(int32_t id)
	{
		if( auto *value = by_wire(id) ) return *value;
		auto value = std::make_unique<stream_state>();
		value->owner = this;
		value->token = next_stream++;
		value->wire_id = id;
		auto *result = value.get();
		wire_to_token[id] = value->token;
		streams.emplace(value->token, std::move(value));
		return *result;
	}

	[[nodiscard]] event_flow emit(protocol_event event)
	{
		if( paused )
		{
			pending_events.push_back({std::move(event), {}});
			return event_flow::continue_;
		}
		if( not event_handler ) return event_flow::continue_;
		return event_handler(event);
	}

	void defer_event(protocol_event event) {
		paused = true;
		pending_events.push_back({std::move(event), {}});
	}

	void defer_body(stream_token token, const uint8_t *data, size_t size)
	{
		auto body = std::make_shared<std::vector<std::byte>>(size);
		if( size != 0 ) std::memcpy(body->data(), data, size);
		pending_events.push_back({body_data_event {token,
			const_buffer(body->data(), body->size())}, std::move(body)});
	}

	[[nodiscard]] body_spec inbound_spec(const stream_state &value,
		bool end_stream) const
	{
		if( end_stream ) return body_spec::empty();
		if( auto length = value.headers.find_first("content-length") )
		{
			uint64_t size = 0;
			auto parsed = std::from_chars(length->data(),
				length->data() + length->size(), size);
			if( parsed.ec == std::errc{} and
				parsed.ptr == length->data() + length->size() )
				return body_spec::known(size);
		}
		return body_spec::streaming();
	}

	static int on_begin_headers(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<impl *>(user_data);
		if( frame->hd.type != NGHTTP2_HEADERS ) return 0;
		stream_state *value = self.by_wire(frame->hd.stream_id);
		if( not value and self.config.role == protocol_role::server and
			frame->headers.cat == NGHTTP2_HCAT_REQUEST )
			value = &self.ensure_inbound(frame->hd.stream_id);
		if( not value ) return 0;
		value->collecting_trailers = value->head_delivered;
		value->headers = {};
		value->method.clear();
		value->scheme.clear();
		value->authority.clear();
		value->path.clear();
		value->protocol_name.clear();
		value->status_code.reset();
		value->header_bytes = 0;
		value->header_count = 0;
		return 0;
	}

	static int on_header(nghttp2_session *, const nghttp2_frame *frame,
		const uint8_t *name, size_t name_size, const uint8_t *field_value,
		size_t value_size, uint8_t, void *user_data)
	{
		auto &self = *static_cast<impl *>(user_data);
		auto *stream = self.by_wire(frame->hd.stream_id);
		if( not stream ) return 0;
		stream->header_bytes += name_size + value_size + 32;
		++stream->header_count;
		if( stream->header_bytes > self.config.limits.max_header_list_size or
			stream->header_count > self.config.limits.max_field_count or
			name_size > self.config.limits.max_field_name_size or
			value_size > self.config.limits.max_field_value_size )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		const std::string_view header_name(
			reinterpret_cast<const char *>(name), name_size);
		const std::string_view header_value(
			reinterpret_cast<const char *>(field_value), value_size);
		try
		{
			if( header_name == ":method" ) stream->method = header_value;
			else if( header_name == ":scheme" ) stream->scheme = header_value;
			else if( header_name == ":authority" ) stream->authority = header_value;
			else if( header_name == ":path" ) stream->path = header_value;
			else if( header_name == ":protocol" ) stream->protocol_name = header_value;
			else if( header_name == ":status" )
			{
				uint16_t status = 0;
				auto parsed = std::from_chars(header_value.data(),
					header_value.data() + header_value.size(), status);
				if( parsed.ec != std::errc{} or
					parsed.ptr != header_value.data() + header_value.size() or
					status < 100 or status > 599 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				stream->status_code = status;
			}
			else
			{
				if( header_name.starts_with(':') or forbidden(header_name) or
					(header_name == "te" and lower(header_value) != "trailers") )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				stream->headers.add(std::string(header_name),
					std::string(header_value));
			}
		}
		catch(...) { return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_frame_recv(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<impl *>(user_data);
		if( frame->hd.type == NGHTTP2_SETTINGS )
		{
			if( not (frame->hd.flags & NGHTTP2_FLAG_ACK) )
			{
				connection_event event {connection_event_kind::settings, {}, {}};
				if( self.emit(event) == event_flow::pause ) self.defer_event(event);
			}
			return 0;
		}
		if( frame->hd.type == NGHTTP2_GOAWAY )
		{
			self.draining = true;
			connection_event event {connection_event_kind::goaway, {}, {}};
			if( auto found = self.wire_to_token.find(frame->goaway.last_stream_id);
				found != self.wire_to_token.end() ) event.last_stream = found->second;
			if( self.emit(event) == event_flow::pause ) self.defer_event(event);
			return 0;
		}
		auto *stream = self.by_wire(frame->hd.stream_id);
		if( not stream ) return 0;
		if( frame->hd.type == NGHTTP2_HEADERS )
		{
			if( stream->collecting_trailers )
			{
				trailers_event event {stream->token, std::move(stream->headers)};
				if( self.emit(event) == event_flow::pause ) self.defer_event(std::move(event));
			}
			else if( self.config.role == protocol_role::server )
			{
				if( stream->method.empty() or stream->authority.empty() )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				request_head head;
				try { head.method = http_exp::method(stream->method); }
				catch(...) { return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE; }
				head.scheme = stream->scheme;
				head.authority = stream->authority;
				head.target = stream->path.empty() ? "/" : stream->path;
				head.headers = std::move(stream->headers);
				if( not stream->protocol_name.empty() )
					head.connect_protocol = stream->protocol_name;
				if( not head.scheme.empty() and not head.authority.empty() )
					head.url = riwo::url(head.scheme + "://" + head.authority +
					(head.target.empty() ? "/" : head.target));
				request_head_event event {stream->token, std::move(head),
					self.inbound_spec(*stream,
						(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0),
					protocol::v2};
				stream->head_delivered = true;
				if( self.emit(event) == event_flow::pause ) self.defer_event(std::move(event));
			}
			else
			{
				if( not stream->status_code )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				response_head head {http_exp::status(*stream->status_code),
					std::move(stream->headers)};
				response_head_event event {stream->token, std::move(head),
					self.inbound_spec(*stream,
						(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0),
					protocol::v2};
				stream->head_delivered = true;
				if( self.emit(event) == event_flow::pause ) self.defer_event(std::move(event));
			}
		}
		if( frame->hd.type == NGHTTP2_RST_STREAM )
		{
			error_info error {
				.code = make_error_code(errc::stream_protocol_error),
				.scope = error_scope::stream_protocol,
				.selected_protocol = protocol::v2,
				.stream_id = frame->hd.stream_id,
				.wire_error_code = frame->rst_stream.error_code
			};
			stream_reset_event event {stream->token, std::move(error)};
			if( self.emit(event) == event_flow::pause ) self.defer_event(std::move(event));
		}
		if( frame->hd.flags & NGHTTP2_FLAG_END_STREAM and not stream->remote_end )
		{
			stream->remote_end = true;
			stream_end_event event {stream->token};
			if( self.emit(event) == event_flow::pause ) self.defer_event(event);
		}
		return 0;
	}

	static int on_data_chunk(nghttp2_session *, uint8_t, int32_t stream_id,
		const uint8_t *data, size_t size, void *user_data)
	{
		auto &self = *static_cast<impl *>(user_data);
		auto *stream = self.by_wire(stream_id);
		if( not stream ) return 0;
		if( self.paused )
		{
			self.defer_body(stream->token, data, size);
			return 0;
		}
		body_data_event event {stream->token,
			const_buffer(reinterpret_cast<const std::byte *>(data), size)};
		if( self.emit(event) == event_flow::pause )
		{
			self.paused = true;
			self.defer_body(stream->token, data, size);
			return NGHTTP2_ERR_PAUSE;
		}
		return 0;
	}

	static int on_stream_close(nghttp2_session *, int32_t stream_id,
		uint32_t error_code_value, void *user_data)
	{
		auto &self = *static_cast<impl *>(user_data);
		auto found = self.wire_to_token.find(stream_id);
		if( found == self.wire_to_token.end() ) return 0;
		auto token = found->second;
		if( error_code_value != NGHTTP2_NO_ERROR )
		{
			error_info error {
				.code = make_error_code(errc::stream_protocol_error),
				.scope = error_scope::stream_protocol,
				.selected_protocol = protocol::v2,
				.stream_id = stream_id,
				.wire_error_code = error_code_value
			};
			stream_reset_event event {token, std::move(error)};
			if( self.emit(event) == event_flow::pause ) self.defer_event(std::move(event));
		}
		if( auto stream = self.streams.find(token);
			stream != self.streams.end() )
			self.outbound_size -= stream->second->outbound_size;
		self.wire_to_token.erase(found);
		self.streams.erase(token);
		return 0;
	}

	static api::ssize_type data_read(nghttp2_session *session, int32_t,
		uint8_t *buffer, size_t length, uint32_t *flags,
		nghttp2_data_source *source, void *)
	{
		auto &value = *static_cast<stream_state *>(source->ptr);
		if( value.outbound.empty() )
		{
			if( not value.outbound_finished ) return NGHTTP2_ERR_DEFERRED;
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.outbound_trailers.empty() and
				not value.trailers_submitted )
			{
				auto trailers = trailer_headers(value.outbound_trailers);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.wire_id,
					trailers->native.data(), trailers->native.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.trailers_submitted = true;
			}
			return 0;
		}
		size_t copied = 0;
		while( copied < length and not value.outbound.empty() )
		{
			auto &chunk = value.outbound.front();
			const auto count = std::min(length - copied,
				chunk.size() - value.outbound_offset);
			std::memcpy(buffer + copied, chunk.data() + value.outbound_offset,
				count);
			copied += count;
			value.outbound_offset += count;
			value.outbound_size -= count;
			value.owner->outbound_size -= count;
			if( value.outbound_offset == chunk.size() )
			{
				value.outbound.pop_front();
				value.outbound_offset = 0;
			}
		}
		if( value.outbound.empty() and value.outbound_finished )
			*flags |= NGHTTP2_DATA_FLAG_EOF;
		return static_cast<api::ssize_type>(copied);
	}

	[[nodiscard]] settings_snapshot snapshot(bool remote) const noexcept
	{
		auto get = [this, remote](nghttp2_settings_id id) {
			return static_cast<uint32_t>(remote ?
				nghttp2_session_get_remote_settings(session, id) :
				nghttp2_session_get_local_settings(session, id));
		};
		return {
			.header_table_size = get(NGHTTP2_SETTINGS_HEADER_TABLE_SIZE),
			.enable_push = get(NGHTTP2_SETTINGS_ENABLE_PUSH),
			.max_concurrent_streams = get(NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS),
			.initial_window_size = get(NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE),
			.max_frame_size = get(NGHTTP2_SETTINGS_MAX_FRAME_SIZE),
			.max_header_list_size = get(NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE),
			.enable_connect_protocol = get(NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL)
		};
	}

	connection_config config;
	nghttp2_session_callbacks *callbacks = nullptr;
	nghttp2_option *option = nullptr;
	nghttp2_session *session = nullptr;
	std::unordered_map<stream_token,std::unique_ptr<stream_state>> streams;
	std::unordered_map<int32_t,stream_token> wire_to_token;
	stream_token next_stream = 1;
	protocol_event_handler event_handler;
	std::deque<pending_event> pending_events;
	size_t paused_replay_bytes = 0;
	std::vector<std::byte> pending_output;
	size_t pending_output_offset = 0;
	size_t outbound_size = 0;
	bool paused = false;
	bool input_finished = false;
	bool draining = false;
};

connection_engine::connection_engine(connection_config config) :
	m_impl(std::make_unique<impl>(std::move(config)))
{
	if( auto error = m_impl->initialize() )
		throw std::system_error(std::error_code(error));
}
connection_engine::connection_engine(connection_engine &&) noexcept = default;
connection_engine &connection_engine::operator=(connection_engine &&) noexcept = default;
connection_engine::~connection_engine() = default;

protocol connection_engine::selected_protocol() const noexcept {
	return protocol::v2;
}

sys_expected<ingest_result> connection_engine::ingest(
	const_buffer input, protocol_event_handler events) noexcept
{
	if( m_impl->input_finished )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));
	try
	{
		m_impl->event_handler = std::move(events);
		m_impl->paused = false;
		while( not m_impl->pending_events.empty() )
		{
			if( m_impl->emit(m_impl->pending_events.front().value) ==
				event_flow::pause )
				return ingest_result {0, true, wants_write(), false};
			m_impl->pending_events.pop_front();
		}
		size_t offset = std::min(input.size(), m_impl->paused_replay_bytes);
		m_impl->paused_replay_bytes -= offset;
		if( m_impl->paused_replay_bytes != 0 )
			return ingest_result {offset, false, wants_write(), true};
		const auto size = input.size() - offset;
		const auto *base = static_cast<const uint8_t *>(input.data());
		const auto *data = size == 0 ? nullptr : base + offset;
		const auto consumed = api::session_mem_recv(m_impl->session,
			data, size);
		if( consumed < 0 )
		{
			if( consumed == NGHTTP2_ERR_PAUSE )
				return ingest_result {offset, true, wants_write(), false};
			return sys_unexpected(protocol_error());
		}
		if( m_impl->paused )
		{
			m_impl->paused_replay_bytes = static_cast<size_t>(consumed);
			return ingest_result {offset, true, wants_write(), false};
		}
		const auto total = offset + static_cast<size_t>(consumed);
		return ingest_result {total, false, wants_write(), total == input.size()};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<ingest_result> connection_engine::finish_input(
	protocol_event_handler events) noexcept
{
	try
	{
		if( m_impl->input_finished )
			return sys_unexpected(make_error_code(errc::operation_not_permitted));
		m_impl->event_handler = std::move(events);
		if( not m_impl->streams.empty() )
		{
			m_impl->input_finished = true;
			return sys_unexpected(make_error_code(errc::unexpected_eof));
		}
		connection_event event {connection_event_kind::input_finished, {}, {}};
		if( m_impl->emit(event) == event_flow::pause )
			return ingest_result {0, true, wants_write(), false};
		m_impl->input_finished = true;
		return ingest_result {0, false, wants_write(), false};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<void> connection_engine::submit(protocol_action action) noexcept
{
	try
	{
		return std::visit([this](auto &&value) -> sys_expected<void>
		{
			using action_type = std::remove_cvref_t<decltype(value)>;
			if constexpr( std::same_as<action_type,open_request_action> )
			{
				if( m_impl->config.role != protocol_role::client or value.stream == 0 or
					m_impl->streams.contains(value.stream) or m_impl->draining or
					m_impl->streams.size() >= m_impl->config.max_concurrent_streams )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				auto headers = request_headers(value.head);
				if( not headers ) return sys_unexpected(headers.error());
				auto state = std::make_unique<impl::stream_state>();
				state->owner = m_impl.get();
				state->token = value.stream;
				state->outbound_finished = value.body.type() == body_spec::mode::empty or
					value.body.type() == body_spec::mode::head or
					(value.body.type() == body_spec::mode::known_length and
					 value.body.length().value_or(0) == 0);
				state->provider = not state->outbound_finished;
				api::data_provider provider {};
				api::data_provider *provider_ptr = nullptr;
				if( state->provider )
				{
					provider.source.ptr = state.get();
					provider.read_callback = &impl::data_read;
					provider_ptr = &provider;
				}
				const auto id = api::submit_request(m_impl->session, nullptr,
					headers->native.data(), headers->native.size(), provider_ptr,
					state.get());
				if( id < 0 ) return sys_unexpected(make_error_code(
					errc::stream_protocol_error));
				state->wire_id = id;
				m_impl->wire_to_token[id] = value.stream;
				m_impl->streams.emplace(value.stream, std::move(state));
				return {};
			}
			else if constexpr( std::same_as<action_type,send_response_action> )
			{
				if( m_impl->config.role != protocol_role::server )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				auto found = m_impl->streams.find(value.stream);
				if( found == m_impl->streams.end() )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				auto headers = response_headers(value.head);
				if( not headers ) return sys_unexpected(headers.error());
				auto &state = *found->second;
				state.outbound_finished = value.body.type() == body_spec::mode::empty or
					value.body.type() == body_spec::mode::head or
					(value.body.type() == body_spec::mode::known_length and
					 value.body.length().value_or(0) == 0);
				state.provider = not state.outbound_finished;
				api::data_provider provider {};
				api::data_provider *provider_ptr = nullptr;
				if( state.provider )
				{
					provider.source.ptr = &state;
					provider.read_callback = &impl::data_read;
					provider_ptr = &provider;
				}
				if( api::submit_response(m_impl->session, state.wire_id,
					headers->native.data(), headers->native.size(), provider_ptr) != 0 )
					return sys_unexpected(make_error_code(errc::stream_protocol_error));
				return {};
			}
			else if constexpr( std::same_as<action_type,send_data_action> )
			{
				auto found = m_impl->streams.find(value.stream);
				if( found == m_impl->streams.end() or not found->second->provider or
					found->second->outbound_finished )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				auto &state = *found->second;
				if( value.data.size() > m_impl->config.max_pending_output or
					state.outbound_size > m_impl->config.max_pending_output -
						value.data.size() or
					m_impl->outbound_size > m_impl->config.max_pending_output -
						value.data.size() )
					return sys_unexpected(make_error_code(errc::buffer_limit_exceeded));
				state.outbound_size += value.data.size();
				m_impl->outbound_size += value.data.size();
				if( not value.data.empty() )
					state.outbound.push_back(std::move(value.data));
				state.outbound_finished = value.end_stream;
				(void)nghttp2_session_resume_data(m_impl->session, state.wire_id);
				return {};
			}
			else if constexpr( std::same_as<action_type,send_trailers_action> )
			{
				auto found = m_impl->streams.find(value.stream);
				if( found == m_impl->streams.end() or not found->second->provider or
					found->second->outbound_finished )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				found->second->outbound_trailers = std::move(value.trailers);
				found->second->outbound_finished = true;
				(void)nghttp2_session_resume_data(m_impl->session,
					found->second->wire_id);
				return {};
			}
			else if constexpr( std::same_as<action_type,reset_stream_action> )
			{
				auto found = m_impl->streams.find(value.stream);
				if( found == m_impl->streams.end() )
					return sys_unexpected(make_error_code(errc::invalid_protocol_action));
				if( nghttp2_submit_rst_stream(m_impl->session, NGHTTP2_FLAG_NONE,
					found->second->wire_id, value.wire_error_code) != 0 )
					return sys_unexpected(protocol_error());
				return {};
			}
			else
			{
				int32_t last = 0;
				if( value.last_stream )
				{
					auto found = m_impl->streams.find(*value.last_stream);
					if( found == m_impl->streams.end() )
						return sys_unexpected(make_error_code(errc::invalid_protocol_action));
					last = found->second->wire_id;
				}
				if( nghttp2_submit_goaway(m_impl->session, NGHTTP2_FLAG_NONE,
					last, value.wire_error_code, nullptr, 0) != 0 )
					return sys_unexpected(protocol_error());
				m_impl->draining = true;
				return {};
			}
		}, std::move(action));
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<produce_result> connection_engine::produce(
	mutable_buffer output) noexcept
{
	if( output.size() == 0 )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));
	try
	{
		if( m_impl->pending_output_offset == m_impl->pending_output.size() )
		{
			m_impl->pending_output.clear();
			m_impl->pending_output_offset = 0;
			const uint8_t *data = nullptr;
			const auto size = api::session_mem_send(m_impl->session, &data);
			if( size < 0 ) return sys_unexpected(protocol_error());
			if( size != 0 )
			{
				if( static_cast<size_t>(size) > m_impl->config.max_pending_output )
					return sys_unexpected(make_error_code(errc::buffer_limit_exceeded));
				m_impl->pending_output.resize(static_cast<size_t>(size));
				std::memcpy(m_impl->pending_output.data(), data,
					static_cast<size_t>(size));
			}
		}
		const auto available = m_impl->pending_output.size() -
			m_impl->pending_output_offset;
		const auto count = std::min(output.size(), available);
		if( count != 0 ) std::memcpy(output.data(),
			m_impl->pending_output.data() + m_impl->pending_output_offset, count);
		m_impl->pending_output_offset += count;
		return produce_result {count, wants_write()};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

bool connection_engine::wants_write() const noexcept {
	return m_impl->pending_output_offset != m_impl->pending_output.size() or
		nghttp2_session_want_write(m_impl->session) != 0;
}

optional<int32_t> connection_engine::wire_stream_id(
	stream_token stream) const noexcept
{
	auto found = m_impl->streams.find(stream);
	if( found == m_impl->streams.end() ) return {};
	return found->second->wire_id;
}

optional<stream_token> connection_engine::stream_from_wire_id(
	int32_t stream_id) const noexcept
{
	auto found = m_impl->wire_to_token.find(stream_id);
	if( found == m_impl->wire_to_token.end() ) return {};
	return found->second;
}

connection_diagnostics connection_engine::diagnostics() const noexcept
{
	return {
		.local_settings = m_impl->snapshot(false),
		.peer_settings = m_impl->snapshot(true),
		.pending_output = m_impl->pending_output.size() -
			m_impl->pending_output_offset + m_impl->outbound_size,
		.draining = m_impl->draining
	};
}

} //namespace riwo::http_exp::v2

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
