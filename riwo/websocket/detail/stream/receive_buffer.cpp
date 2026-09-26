// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/receive_buffer.h>
#include <riwo/websocket/detail/permessage_deflate.h>
#include <riwo/websocket/protocol/detail/utf8.h>
#include <riwo/websocket/protocol/generator.h>

namespace riwo::websocket::detail
{

void receive_buffer::reset(role local_role, const stream_config &config,
	std::span<const extension> extensions, std::vector<std::byte> pending_data)
{
	auto compression = make_permessage_deflate_runtime(extensions, local_role);
	if( not compression )
		throw system_error(compression.error());

	if( compression->enabled )
	{
		if( auto error = m_inflater.reset(compression->incoming_window_bits,
			compression->incoming_no_context_takeover) )
			throw system_error(error);
	}
	frame_codec_config codec_config {
		.local_role = local_role, .max_frame_size = config.max_frame_size
	};
	if( not extensions.empty() )
		codec_config.allowed_rsv = reserved_bit::rsv1;

	auto parser = std::make_unique<frame_parser>(codec_config);
	auto read_buffer = std::make_shared<std::vector<std::byte>>(config.read_buffer_size);

	m_parser = std::move(parser);
	m_read_buffer = std::move(read_buffer);
	m_pending_data = std::move(pending_data);

	m_max_message_size = config.max_message_size;
	m_compression = *compression;

	m_permessage_deflate = compression->enabled;
	m_message_compressed = false;

	m_pending_offset = 0;
	m_read_size = 0;
	m_read_offset = 0;

	m_message_type.reset();
	m_message_target.reset();
	m_message_body.clear();

	m_message_wire_size = 0;
	m_message_size = 0;

	m_chunk_first = true;
	m_chunk_utf8.reset();

	m_frame_offset = 0;
	m_control_body.clear();
}

mutable_buffer receive_buffer::available_data() noexcept
{
	if( m_pending_offset < m_pending_data.size() )
	{
		const auto available = m_pending_data.size() - m_pending_offset;
		const auto size = m_read_buffer ?
			std::min(available, m_read_buffer->size()) : available;
		return {
			m_pending_data.data() + m_pending_offset,
			size
		};
	}
	if( m_read_offset < m_read_size )
	{
		return {
			m_read_buffer->data() + m_read_offset,
			m_read_size - m_read_offset
		};
	}
	return {};
}

std::shared_ptr<std::vector<std::byte>> receive_buffer::read_storage() const noexcept
{
	return m_read_buffer;
}

error_code receive_buffer::commit_read(size_t size) noexcept
{
	if( not m_read_buffer or size > m_read_buffer->size() )
		return make_system_error_code(std::errc::io_error);

	m_read_size = size;
	m_read_offset = 0;
	return {};
}

error_code receive_buffer::target_error(receive_target target) const noexcept
{
	if( m_message_target and *m_message_target != target )
		return make_system_error_code(std::errc::operation_not_supported);
	return {};
}

sys_expected<optional<received_event>> receive_buffer::consume(receive_target target) noexcept
{
	if( not m_pending_data.empty() and m_pending_offset == m_pending_data.size() )
	{
		m_pending_data.clear();
		m_pending_offset = 0;
	}
	auto input = available_data();
	if( input.size() == 0 )
		return optional<received_event>{};

	if( not m_parser )
		return sys_unexpected(make_system_error_code(std::errc::io_error));

	auto parsed = m_parser->parse(input);
	if( not parsed )
		return sys_unexpected(parsed.error());

	if( parsed->consumed == 0 )
		return sys_unexpected(make_system_error_code(std::errc::io_error));

	const auto &header = m_parser->header();
	if( parsed->header_ready )
	{
		const bool compressed = header.rsv.test_flag(reserved_bit::rsv1);
		if( compressed and (not m_permessage_deflate or
			header.op == opcode::continuation or is_control_opcode(header.op)) )
			return sys_unexpected(make_error_code(protocol_errc::unexpected_rsv));

		if( header.op == opcode::text or header.op == opcode::binary )
		{
			m_message_type = header.op == opcode::text ?
				message_type::text : message_type::binary;

			m_message_target = target;
			m_message_body.clear();

			m_message_wire_size = 0;
			m_message_size = 0;

			m_chunk_first = true;
			m_chunk_utf8.reset();
			m_message_compressed = compressed;

			if( target == receive_target::chunk )
			{
				if( *m_message_type == message_type::text )
					m_chunk_utf8.emplace();
			}
		}
		else if( header.op == opcode::continuation )
		{
			if( not m_message_type )
				return sys_unexpected(make_error_code(protocol_errc::unexpected_continuation));

			if( auto error = target_error(target) )
				return sys_unexpected(error);
		}
		if( is_data_opcode(header.op) or header.op == opcode::continuation )
			m_frame_offset = m_message_body.size();

		else if( is_control_opcode(header.op) )
			m_control_body.clear();

		if( is_data_opcode(header.op) or header.op == opcode::continuation )
		{
			if( header.payload_size > std::numeric_limits<size_t>::max() or
				static_cast<size_t>(header.payload_size) >
					std::numeric_limits<size_t>::max() - m_message_wire_size )
				return sys_unexpected(make_error_code(errc::message_too_big));

			size_t wire_limit = m_max_message_size;
			if( m_message_compressed and wire_limit != 0 )
			{
				const auto overhead = wire_limit / 8 + 1024;
				wire_limit = overhead > std::numeric_limits<size_t>::max() - wire_limit ?
					std::numeric_limits<size_t>::max() : wire_limit + overhead;
			}
			if( wire_limit != 0 and
				static_cast<size_t>(header.payload_size) >
					wire_limit - std::min(m_message_wire_size, wire_limit) )
				return sys_unexpected(make_error_code(errc::message_too_big));
		}
	}
	optional<message_chunk> chunk;
	std::vector<std::byte> decoded_payload;

	bool decoded_payload_owned = false;
	bool compression_finalized = false;

	if( parsed->payload.size() != 0 )
	{
		if( header.mask )
			apply_mask(parsed->payload, *header.mask, parsed->payload_offset);
		try {
			const auto *begin = static_cast<const std::byte*>(parsed->payload.data());
			if( is_control_opcode(header.op) )
			{
				m_control_body.insert(m_control_body.end(), begin,
					begin + parsed->payload.size());
			}
			else
			{
				m_message_wire_size += parsed->payload.size();
				const std::byte *application_begin = begin;
				size_t application_size = parsed->payload.size();

				if( m_message_compressed )
				{
					const bool final = parsed->frame_finished and header.fin;

					const auto current_size = target == receive_target::chunk ?
						m_message_size : m_message_body.size();

					const auto remaining = m_max_message_size == 0 ?
						0 : m_max_message_size - std::min(current_size, m_max_message_size);

					auto decoded = m_inflater.inflate_chunk (
						std::span(begin, parsed->payload.size()), final, remaining
					);
					if( not decoded )
						return sys_unexpected(decoded.error());

					decoded_payload = std::move(*decoded);
					decoded_payload_owned = true;
					compression_finalized = final;

					application_begin = decoded_payload.data();
					application_size = decoded_payload.size();
				}
				if( target == receive_target::chunk )
				{
					if( not m_message_type )
						return sys_unexpected(make_system_error_code(std::errc::io_error));

					if( *m_message_type == message_type::text and application_size != 0 )
					{
						const auto text = std::string_view (
							reinterpret_cast<const char*>(application_begin), application_size
						);
						if( not m_chunk_utf8 or not m_chunk_utf8->consume(text) )
							return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
					}
					if( application_size != 0 )
					{
						chunk = message_chunk {
							.type = *m_message_type,
							.body = const_buffer(application_begin, application_size),
							.offset = m_message_size,
							.first = m_chunk_first,
							.last = parsed->frame_finished and header.fin,
						};
						m_message_size += application_size;
						m_chunk_first = false;
					}
				}
				else
				{
					m_message_body.insert(m_message_body.end(),
						application_begin, application_begin + application_size
					);
					m_message_size = m_message_body.size();
				}
			}
		}
		catch(const std::bad_alloc&) {
			return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
		}
		catch(...) {
			return sys_unexpected(make_system_error_code(std::errc::io_error));
		}
	}
	if( parsed->frame_finished and header.fin and m_message_compressed and
		(is_data_opcode(header.op) or header.op == opcode::continuation) and
		not compression_finalized )
	{
		const auto current_size = target == receive_target::chunk ?
			m_message_size : m_message_body.size();

		const auto remaining = m_max_message_size == 0 ?
			0 : m_max_message_size - std::min(current_size, m_max_message_size);

		auto decoded = m_inflater.inflate_chunk({}, true, remaining);
		if( not decoded )
			return sys_unexpected(decoded.error());

		decoded_payload = std::move(*decoded);
		decoded_payload_owned = true;

		if( target == receive_target::chunk )
		{
			if( not decoded_payload.empty() )
			{
				if( *m_message_type == message_type::text )
				{
					const auto text = std::string_view (
						reinterpret_cast<const char*>(decoded_payload.data()),
						decoded_payload.size()
					);
					if( not m_chunk_utf8 or not m_chunk_utf8->consume(text) )
						return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
				}
				chunk = message_chunk {
					.type = *m_message_type,
					.body = const_buffer(decoded_payload.data(), decoded_payload.size()),
					.offset = m_message_size,
					.first = m_chunk_first,
					.last = true,
				};
				m_message_size += decoded_payload.size();
				m_chunk_first = false;
			}
		}
		else
		{
			m_message_body.insert(m_message_body.end(),
				decoded_payload.begin(), decoded_payload.end());
			m_message_size = m_message_body.size();
		}
	}
	if( m_pending_offset < m_pending_data.size() )
	{
		m_pending_offset += parsed->consumed;
		if( m_pending_offset == m_pending_data.size() and not chunk )
		{
			m_pending_data.clear();
			m_pending_offset = 0;
		}
	}
	else
	{
		m_read_offset += parsed->consumed;
		if( m_read_offset == m_read_size )
		{
			m_read_offset = 0;
			m_read_size = 0;
		}
	}
	if( not parsed->frame_finished )
	{
		if( chunk )
		{
			received_event event {};
			event.op = header.op;

			if( decoded_payload_owned )
			{
				event.chunk_storage = std::move(decoded_payload);
				chunk->body = const_buffer(event.chunk_storage.data(),
					event.chunk_storage.size());
			}
			event.chunk = chunk;
			return optional(std::move(event));
		}
		return optional<received_event>{};
	}
	received_event event {};
	event.op = header.op;

	if( is_data_opcode(header.op) or header.op == opcode::continuation )
	{
		if( not m_message_type )
			return sys_unexpected(make_error_code(protocol_errc::unexpected_continuation));

		if( target == receive_target::frame )
		{
			try {
				event.frame = data_frame {
					.type = *m_message_type,
					.body = std::vector<std::byte> {
						m_message_body.begin() + static_cast<std::ptrdiff_t>(m_frame_offset),
						m_message_body.end()
					},
					.continuation = header.op == opcode::continuation,
					.fin = header.fin,
				};
			}
			catch(const std::bad_alloc&) {
				return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
			}
			catch(...) {
				return sys_unexpected(make_system_error_code(std::errc::io_error));
			}
		}
		if( header.fin )
		{
			if( *m_message_type == message_type::text and target != receive_target::chunk )
			{
				const auto text = m_message_body.empty() ?
					std::string_view{} :
					std::string_view (
						reinterpret_cast<const char*>(m_message_body.data()),
						m_message_body.size()
					);
				if( not is_valid_utf8(text) )
					return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
			}
			if( target == receive_target::message )
			{
				event.data = message {
					.type = *m_message_type,
					.body = std::move(m_message_body),
				};
			}
			else if( target == receive_target::chunk )
			{
				if( *m_message_type == message_type::text and
					(not m_chunk_utf8 or not m_chunk_utf8->complete()) )
					return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));

				if( chunk )
				{
					if( decoded_payload_owned )
					{
						event.chunk_storage = std::move(decoded_payload);
						chunk->body = const_buffer (
							event.chunk_storage.data(), event.chunk_storage.size()
						);
					}
					event.chunk = chunk;
				}
				else
				{
					event.chunk = message_chunk {
						.type = *m_message_type,
						.body = {},
						.offset = m_message_size,
						.first = m_chunk_first,
						.last = true,
					};
				}
			}
			m_message_type.reset();
			m_message_target.reset();

			m_message_compressed = false;
			m_message_body.clear();

			m_message_wire_size = 0;
			m_message_size = 0;

			m_chunk_first = true;
			m_chunk_utf8.reset();
		}
		else if( target == receive_target::message )
			return optional<received_event>{};

		else if( target == receive_target::chunk )
		{
			if( chunk )
			{
				if( decoded_payload_owned )
				{
					event.chunk_storage = std::move(decoded_payload);
					chunk->body = const_buffer (
						event.chunk_storage.data(), event.chunk_storage.size()
					);
				}
				event.chunk = chunk;
			}
			else
				return optional<received_event>{};
		}
	}
	else
		event.control = std::move(m_control_body);

	return optional(std::move(event));
}

} //namespace riwo::websocket::detail
