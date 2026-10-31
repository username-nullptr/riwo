// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "engine.h"

namespace riwo::experimental::http::v1 { namespace
{

[[nodiscard]] body_spec spec_for(body_framing framing, uint64_t content_length)
{
	switch( framing )
	{
	case body_framing::content_length:
		return body_spec::known(content_length);

	case body_framing::chunked:
	case body_framing::until_eof:
		return body_spec::streaming();

	default: break;
	}
	return body_spec::empty();
}

[[nodiscard]] bool ends_immediately(const body_spec &spec) noexcept
{
	return spec.type() == body_spec::mode::empty or
		   spec.type() == body_spec::mode::head or
		   (spec.type() == body_spec::mode::known_length and spec.length().value_or(0) == 0);
}

template<class Event>
[[nodiscard]] event_flow deliver(const protocol_event_handler_t &handler, Event &&event)
{
	if( not handler )
		return event_flow::continue_;
	return handler(protocol_event_t(std::forward<Event>(event)));
}

} //namespace

class RIWO_DECL_HIDDEN connection_engine::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	explicit impl(const connection_config &value) :
		config(value), request_parse(config.limits),
		response_parse(config.limits), chunks(config.limits) {}

	[[nodiscard]] sys_expected<void> append_output(const std::string &bytes) noexcept
	try
	{
		const auto active = output.size() - output_offset;
		if( bytes.size() > config.max_pending_output or active > config.max_pending_output - bytes.size() )
			return sys_unexpected(make_error_code(errc::buffer_limit_exceeded));

		if( output_offset != 0 )
		{
			output.erase(0, output_offset);
			output_offset = 0;
		}
		output += bytes;
		return {};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}

	[[nodiscard]] event_flow emit_end(const protocol_event_handler_t &events)
	{
		if( incoming_end_delivered )
			return event_flow::Continue;

		const auto flow = deliver(events, stream_end_event{incoming_stream});
		if( flow == event_flow::Continue )
		{
			incoming_end_delivered = true;
			reset_incoming();
		}
		return flow;
	}

	void reset_incoming()
	{
		if( config.role == protocol_role::client )
		{
			active_stream = 0;
			outgoing_stream = 0;
			outgoing_finished = true;
		}
		incoming_stream = 0;
		incoming_framing = body_framing::none;

		incoming_remaining = 0;
		incoming_end_delivered = false;

		chunks.reset();
		request_parse.reset();
		response_parse.reset(next_head_request);

		pending_head_input = false;
	}

public:
	connection_config config {};
	request_parser request_parse {};
	response_parser response_parse {};
	chunk_decoder chunks {};

	std::string output {};
	size_t output_offset = 0;

	stream_token_t next_stream = 1;
	stream_token_t incoming_stream = 0;
	stream_token_t outgoing_stream = 0;
	stream_token_t active_stream = 0;

	body_framing incoming_framing = body_framing::none;
	body_framing outgoing_framing = body_framing::none;

	uint64_t incoming_remaining = 0;
	uint64_t outgoing_remaining = 0;

	bool incoming_end_delivered = false;
	bool outgoing_finished = true;
	bool input_finished = false;

	bool shutdown = false;
	bool next_head_request = false;
	bool pending_head_input = false;

	optional<protocol_event_t> paused_event {};
	std::vector<std::byte> paused_body {};

	size_t paused_replay_bytes = 0;
};

connection_engine::connection_engine(const connection_config &config) :
	m_impl(std::make_unique<impl>(config))
{
	if( m_impl->config.version != protocol::v1_0 and
		m_impl->config.version != protocol::v1_1 )
		invalid_argument::loc_throw("v1::connection_engine requires HTTP/1.x");

	if( m_impl->config.max_pending_output == 0 )
		invalid_argument::loc_throw("v1::connection_engine output limit is zero");
}

connection_engine::connection_engine(connection_engine &&other) noexcept :
	m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<impl>(connection_config{});
}

connection_engine &connection_engine::operator=(connection_engine &&other) noexcept
{
	if( this == &other )
		return *this;
	m_impl = std::move(other.m_impl);
	other.m_impl = std::make_unique<impl>(connection_config{});
	return *this;
}
connection_engine::~connection_engine() = default;

protocol connection_engine::selected_protocol() const noexcept
{
	return m_impl->config.version;
}

sys_expected<ingest_result> connection_engine::ingest
(const const_buffer &input, const protocol_event_handler_t &events) noexcept
try
{
	if( m_impl->input_finished )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));

	const auto *bytes = static_cast<const std::byte *>(input.data());
	size_t offset = 0;

	if( m_impl->paused_event )
	{
		const bool completes_stream =
			std::holds_alternative<stream_end_event>(*m_impl->paused_event);

		const bool completes_head =
			std::holds_alternative<request_head_event>(*m_impl->paused_event) or
			std::holds_alternative<response_head_event>(*m_impl->paused_event);

		const bool completes_trailers =
			std::holds_alternative<trailers_event>(*m_impl->paused_event);

		if( deliver(events, *m_impl->paused_event) == event_flow::pause )
			return ingest_result {0, true, wants_write(), false};

		m_impl->paused_event.reset();
		m_impl->paused_body.clear();

		if( completes_stream )
			m_impl->reset_incoming();

		offset = std::min(input.size(), m_impl->paused_replay_bytes);
		m_impl->paused_replay_bytes -= offset;

		if( m_impl->paused_replay_bytes != 0 )
			return ingest_result {offset, false, wants_write(), true};

		if( (completes_head and m_impl->incoming_framing == body_framing::none) or
			completes_trailers )
		{
			if( m_impl->emit_end(events) == event_flow::pause )
			{
				m_impl->paused_event = stream_end_event {m_impl->incoming_stream};
				return ingest_result {offset, true, wants_write(), false};
			}
		}
	}
	while( offset < input.size() )
	{
		if( m_impl->incoming_stream == 0 )
		{
			if( m_impl->config.role == protocol_role::client and m_impl->outgoing_stream == 0 )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			const_buffer tail(bytes + offset, input.size() - offset);
			if( tail.size() != 0 )
				m_impl->pending_head_input = true;

			if( m_impl->config.role == protocol_role::server )
			{
				if( m_impl->active_stream != 0 )
					return sys_unexpected(make_error_code(errc::operation_in_progress));

				auto parsed = m_impl->request_parse.ingest(tail);
				if( not parsed )
					return sys_unexpected(parsed.error());

				offset += parsed->consumed;
				if( not parsed->head )
					return ingest_result {offset, false, wants_write(), true};

				m_impl->pending_head_input = false;
				m_impl->incoming_stream = m_impl->next_stream++;
				m_impl->active_stream = m_impl->incoming_stream;

				m_impl->incoming_framing = parsed->head->framing;
				m_impl->incoming_remaining = parsed->head->content_length;

				request_head_event event {
					.stream = m_impl->incoming_stream,
					.head = std::move(parsed->head->head),
					.body = spec_for(m_impl->incoming_framing, m_impl->incoming_remaining),
					.version = parsed->head->version
				};
				if( deliver(events, event) == event_flow::pause )
				{
					m_impl->paused_event = std::move(event);
					m_impl->paused_replay_bytes = parsed->consumed;

					return ingest_result {
						.consumed = offset - parsed->consumed,
						.paused = true,
						.wants_write = wants_write(),
						.needs_input = false
					};
				}
			}
			else
			{
				auto parsed = m_impl->response_parse.ingest(tail);
				if( not parsed )
					return sys_unexpected(parsed.error());

				offset += parsed->consumed;
				if( not parsed->head )
				{
					return ingest_result {
						.consumed = offset,
						.paused = false,
						.wants_write = wants_write(),
						.needs_input = true
					};
				}
				m_impl->pending_head_input = false;
				m_impl->incoming_stream = m_impl->active_stream;

				m_impl->incoming_framing = parsed->head->framing;
				m_impl->incoming_remaining = parsed->head->content_length;

				response_head_event event {
					.stream = m_impl->incoming_stream,
					.head = std::move(parsed->head->head),
					.body = spec_for(m_impl->incoming_framing,
						m_impl->incoming_remaining),
					.version = parsed->head->version
				};
				if( deliver(events, event) == event_flow::pause )
				{
					m_impl->paused_event = std::move(event);
					m_impl->paused_replay_bytes = parsed->consumed;

					return ingest_result {
						.consumed = offset - parsed->consumed,
						.paused = true,
						.wants_write = wants_write(),
						.needs_input = false
					};
				}
			}
			if( m_impl->incoming_framing == body_framing::none )
			{
				if( m_impl->emit_end(events) == event_flow::pause )
				{
					m_impl->paused_event = stream_end_event {m_impl->incoming_stream};
					return ingest_result {offset, true, wants_write(), false};
				}
				continue;
			}
		}
		if( m_impl->incoming_framing == body_framing::content_length )
		{
			const auto count = static_cast<size_t>(std::min<uint64_t>(
				m_impl->incoming_remaining, input.size() - offset
			));
			body_data_event event {
				.stream = m_impl->incoming_stream,
				.data = const_buffer(bytes + offset, count)
			};
			if( count != 0 and deliver(events, event) == event_flow::pause )
				return ingest_result {offset, true, wants_write(), false};

			offset += count;
			m_impl->incoming_remaining -= count;

			if( m_impl->incoming_remaining != 0 )
				return ingest_result {offset, false, wants_write(), true};

			if( m_impl->emit_end(events) == event_flow::pause )
			{
				m_impl->paused_event = stream_end_event {m_impl->incoming_stream};
				return ingest_result {offset, true, wants_write(), false};
			}
			continue;
		}
		if( m_impl->incoming_framing == body_framing::chunked )
		{
			auto decoded = m_impl->chunks.ingest (
				const_buffer(bytes + offset, input.size() - offset)
			);
			if( not decoded )
				return sys_unexpected(decoded.error());

			offset += decoded->consumed;
			if( not decoded->data.empty() )
			{
				body_data_event event {
					.stream = m_impl->incoming_stream,
					.data = const_buffer(decoded->data.data(), decoded->data.size())
				};
				if( deliver(events, event) == event_flow::pause )
				{
					m_impl->paused_body = std::move(decoded->data);

					m_impl->paused_event = body_data_event{
						.stream = m_impl->incoming_stream,
						.data = const_buffer (
							m_impl->paused_body.data(), m_impl->paused_body.size()
						)
					};
					m_impl->paused_replay_bytes = decoded->consumed;

					return ingest_result {
						.consumed = offset - decoded->consumed,
						.paused = true,
						.wants_write = wants_write(),
						.needs_input = false
					};
				}
			}
			if( decoded->trailers and not decoded->trailers->empty() )
			{
				trailers_event event {
					.stream = m_impl->incoming_stream,
					.trailers = std::move(*decoded->trailers)
				};
				if( deliver(events, event) == event_flow::pause )
				{
					m_impl->paused_event = std::move(event);
					m_impl->paused_replay_bytes = decoded->consumed;

					return ingest_result {
						.consumed = offset - decoded->consumed,
						.paused = true,
						.wants_write = wants_write(),
						.needs_input = false
					};
				}
			}
			if( decoded->message_end )
			{
				if( m_impl->emit_end(events) == event_flow::pause )
				{
					m_impl->paused_event = stream_end_event {m_impl->incoming_stream};
					return ingest_result {offset, true, wants_write(), false};
				}
				continue;
			}
			return ingest_result {offset, false, wants_write(), true};
		}

		if( m_impl->incoming_framing == body_framing::until_eof )
		{
			const auto count = input.size() - offset;
			body_data_event event {
				.stream = m_impl->incoming_stream,
				.data = const_buffer(bytes + offset, count)
			};
			if( count != 0 and deliver(events, event) == event_flow::pause )
				return ingest_result {offset, true, wants_write(), false};

			offset += count;
			return ingest_result {offset, false, wants_write(), true};
		}
	}
	return ingest_result {offset, false, wants_write(), true};
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

sys_expected<ingest_result>
connection_engine::finish_input(const protocol_event_handler_t &events) noexcept
try
{
	if( m_impl->input_finished )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));

	if( m_impl->paused_event )
	{
		const bool completes_stream =
			std::holds_alternative<stream_end_event>(*m_impl->paused_event);

		if( deliver(events, *m_impl->paused_event) == event_flow::pause )
			return ingest_result {0, true, wants_write(), false};

		m_impl->paused_event.reset();
		m_impl->paused_body.clear();

		if( completes_stream )
			m_impl->reset_incoming();
	}
	if( m_impl->incoming_stream != 0 and m_impl->incoming_framing == body_framing::until_eof )
	{
		if( m_impl->emit_end(events) == event_flow::pause )
			return ingest_result {0, true, wants_write(), false};
	}
	else if( m_impl->pending_head_input or m_impl->incoming_stream != 0 or
		(m_impl->config.role == protocol_role::client and m_impl->outgoing_stream != 0) )
	{
		m_impl->input_finished = true;
		return sys_unexpected(make_error_code(errc::unexpected_eof));
	}
	if( deliver(events, connection_event {
		.kind = connection_event_kind::input_finished,
		.error = {},
		.last_stream = {}
	}) == event_flow::pause )
		return ingest_result {0, true, wants_write(), false};

	m_impl->input_finished = true;
	return ingest_result {
		.consumed = 0,
		.paused = false,
		.wants_write = wants_write(),
		.needs_input = false
	};
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

sys_expected<void> connection_engine::submit(protocol_action_t action) noexcept
try
{
	if( m_impl->shutdown and not std::holds_alternative<shutdown_action>(action) )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));

	return std::visit([this]<typename Action>(Action &&value) -> sys_expected<void>
	{
		using action_t = std::remove_cvref_t<Action>;
		if constexpr( std::same_as<action_t,open_request_action> )
		{
			if( m_impl->config.role != protocol_role::client or
				value.stream == 0 or m_impl->outgoing_stream != 0 )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			auto encoded = serialize_request_head(value.head, value.body,
				m_impl->config.version, m_impl->config.target_form);

			if( not encoded )
				return sys_unexpected(encoded.error());

			auto appended = m_impl->append_output(std::move(encoded->wire));
			if( not appended )
				return appended;

			m_impl->outgoing_stream = value.stream;
			m_impl->active_stream = value.stream;

			m_impl->outgoing_framing = encoded->framing;
			m_impl->outgoing_remaining = value.body.length().value_or(0);
			m_impl->outgoing_finished = ends_immediately(value.body);

			m_impl->next_head_request = value.head.method.known() == method::head;
			m_impl->response_parse.reset(m_impl->next_head_request);
			return {};
		}
		else if constexpr( std::same_as<action_t,send_response_action> )
		{
			if( m_impl->config.role != protocol_role::server or
				value.stream == 0 or value.stream != m_impl->active_stream or
				m_impl->outgoing_stream != 0 )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			auto encoded = serialize_response_head (
				value.head, value.body, m_impl->config.version
			);
			if( not encoded )
				return sys_unexpected(encoded.error());

			auto appended = m_impl->append_output(std::move(encoded->wire));
			if( not appended )
				return appended;

			m_impl->outgoing_stream = value.stream;
			m_impl->outgoing_framing = encoded->framing;

			m_impl->outgoing_remaining = value.body.length().value_or(0);
			m_impl->outgoing_finished = ends_immediately(value.body);
			return {};
		}
		else if constexpr( std::same_as<action_t,send_data_action> )
		{
			if( value.stream == 0 or value.stream != m_impl->outgoing_stream or m_impl->outgoing_finished )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			std::string wire;
			if( m_impl->outgoing_framing == body_framing::content_length )
			{
				if( value.data.size() > m_impl->outgoing_remaining )
					return sys_unexpected(make_error_code(errc::body_length_mismatch));

				wire.assign (
					reinterpret_cast<const char*>(value.data.data()),
					value.data.size()
				);
				m_impl->outgoing_remaining -= value.data.size();

				if( value.end_stream and m_impl->outgoing_remaining != 0 )
					return sys_unexpected(make_error_code(errc::body_length_mismatch));

				m_impl->outgoing_finished = m_impl->outgoing_remaining == 0;
			}
			else if( m_impl->outgoing_framing == body_framing::chunked )
			{
				chunk_encoder encoder;
				if( not value.data.empty() )
				{
					auto chunk = encoder.encode (
						const_buffer(value.data.data(), value.data.size())
					);
					if( not chunk )
						return sys_unexpected(chunk.error());
					wire = std::move(*chunk);
				}
				if( value.end_stream )
				{
					auto last = encoder.finish();
					if( not last )
						return sys_unexpected(last.error());

					wire += *last;
					m_impl->outgoing_finished = true;
				}
			}
			else
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			return m_impl->append_output(wire);
		}
		else if constexpr( std::same_as<action_t,send_trailers_action> )
		{
			if( value.stream == 0 or value.stream != m_impl->outgoing_stream or
				m_impl->outgoing_finished or m_impl->outgoing_framing != body_framing::chunked )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			chunk_encoder encoder;
			auto last = encoder.finish(value.trailers);

			if( not last )
				return sys_unexpected(last.error());

			m_impl->outgoing_finished = true;
			return m_impl->append_output(std::move(*last));
		}
		else if constexpr( std::same_as<action_t,reset_stream_action> )
		{
			if( value.stream == 0 or (value.stream != m_impl->incoming_stream and
				value.stream != m_impl->outgoing_stream) )
				return sys_unexpected(make_error_code(errc::invalid_protocol_action));

			m_impl->shutdown = true;
			return {};
		}
		else
		{
			m_impl->shutdown = true;
			return {};
		}
	},
	std::move(action));
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

sys_expected<produce_result> connection_engine::produce(const mutable_buffer &output) noexcept
{
	if( output.size() == 0 )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));

	const auto available = m_impl->output.size() - m_impl->output_offset;
	const auto count = std::min(output.size(), available);

	if( count != 0 )
	{
		std::memcpy(output.data(),
			m_impl->output.data() + m_impl->output_offset, count
		);
	}
	m_impl->output_offset += count;
	const bool more = m_impl->output_offset != m_impl->output.size();
	if( not more )
	{
		m_impl->output.clear();
		m_impl->output_offset = 0;

		if( m_impl->config.role == protocol_role::server and m_impl->outgoing_finished )
		{
			m_impl->outgoing_stream = 0;
			m_impl->active_stream = 0;
		}
	}
	return produce_result {count, more};
}

bool connection_engine::wants_write() const noexcept
{
	return m_impl->output_offset != m_impl->output.size();
}

} //namespace riwo::experimental::http::v1
