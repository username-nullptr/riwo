// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/stream/frame_builder.h>
#include <riwo/websocket/detail/permessage_deflate.h>
#include <riwo/websocket/detail/secure_random.h>
#include <riwo/websocket/protocol/detail/utf8.h>
#include <riwo/websocket/protocol/generator.h>

namespace riwo::websocket::detail
{

frame_builder::frame_builder
(role local_role, const stream_config &config, std::span<const extension> extensions) noexcept
{
	reset(local_role, config, extensions);
}

frame_builder &frame_builder::reset
(role local_role, const stream_config &config, std::span<const extension> extensions) noexcept
{
	m_role = local_role;

	m_max_frame_size = config.max_frame_size;
	m_max_message_size = config.max_message_size;

	m_fragment_size = config.write_fragment_size;
	m_compression_config = config.compression;

	auto compression = make_permessage_deflate_runtime(extensions, local_role);
	m_compression = compression ?
		*compression : permessage_deflate_runtime {};

	return *this;
}

sys_expected<prepared_frame> frame_builder::prepare_control
(opcode op, const const_buffer &payload, bool borrow_payload) const noexcept
{
	try {
		if( op != opcode::ping and op != opcode::pong and op != opcode::close )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( payload.size() > 125 )
			return sys_unexpected(make_error_code(protocol_errc::ctrl_payload_too_large));

		if( payload.size() != 0 and payload.data() == nullptr )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		frame_header header{.op = op, .payload_size = payload.size()};
		if( m_role == role::client )
		{
			masking_key key;
			auto random = secure_random_bytes (
				mutable_buffer(key.bytes.data(), key.bytes.size())
			);
			if( not random )
				return sys_unexpected(random.error());
			header.mask = key;
		}
		auto encoded = encode_frame_header(header, frame_codec_config {
			.local_role = m_role, .max_frame_size = m_max_frame_size
		});
		if( not encoded )
			return sys_unexpected(encoded.error());

		const bool borrowed = borrow_payload and
			not header.mask and payload.size() != 0;

		auto wire = std::make_shared<std::vector<std::byte>>(
			encoded->size + (borrowed ? 0 : payload.size())
		);
		std::memcpy(wire->data(), encoded->buffer().data(), encoded->size);
		if( borrowed )
		{
			prepared_frame result;
			result.wire = std::move(wire);

			result.buffers = {
				const_buffer(result.wire->data(), result.wire->size()),
				payload
			};
			result.header_size = encoded->size;
			result.payload_size = payload.size();
			result.application_size = payload.size();
			return result;
		}
		if( header.mask )
		{
			auto copied = mask_copy (
				mutable_buffer(wire->data() + encoded->size, payload.size()),
				payload, *header.mask
			);
			if( not copied )
				return sys_unexpected(copied.error());
		}
		else if( payload.size() != 0 )
			std::memcpy(wire->data() + encoded->size, payload.data(), payload.size());

		prepared_frame result;
		result.wire = std::move(wire);

		result.buffers = {
			const_buffer(result.wire->data(), result.wire->size())
		};
		result.header_size = encoded->size;
		result.payload_size = payload.size();
		result.application_size = payload.size();
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

sys_expected<prepared_frame> frame_builder::prepare_close(const close_frame &frame) const noexcept
{
	auto payload = encode_close_payload(frame);
	if( not payload )
		return sys_unexpected(payload.error());
	return prepare_control(opcode::close, payload->buffer());
}

sys_expected<prepared_frame> frame_builder::prepare_data_frame
(message_type type, const const_buffer &payload, bool continuation, bool fin, bool borrow_payload) const noexcept
{
	try {
		if( type != message_type::text and type != message_type::binary )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		if( payload.size() != 0 and payload.data() == nullptr )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		frame_header header {
			.fin = fin,
			.op = continuation ? opcode::continuation :
				type == message_type::text ? opcode::text : opcode::binary,
			.payload_size = payload.size(),
		};
		if( m_role == role::client )
		{
			masking_key key;
			auto random = secure_random_bytes (
				mutable_buffer(key.bytes.data(), key.bytes.size())
			);
			if( not random )
				return sys_unexpected(random.error());
			header.mask = key;
		}
		auto encoded = encode_frame_header(header, frame_codec_config {
			.local_role = m_role, .max_frame_size = m_max_frame_size
		});
		if( not encoded )
			return sys_unexpected(encoded.error());

		const bool borrowed = borrow_payload and
			not header.mask and payload.size() != 0;

		auto wire = std::make_shared<std::vector<std::byte>>(
			encoded->size + (borrowed ? 0 : payload.size())
		);
		std::memcpy(wire->data(), encoded->buffer().data(), encoded->size);

		prepared_frame result;
		result.wire = std::move(wire);
		result.header_size = encoded->size;
		result.payload_size = payload.size();
		result.application_size = payload.size();

		if( borrowed )
		{
			result.buffers = {
				const_buffer(result.wire->data(), result.wire->size()), payload
			};
			return result;
		}
		if( header.mask )
		{
			auto copied = mask_copy (
				mutable_buffer(result.wire->data() + encoded->size, payload.size()),
				payload, *header.mask
			);
			if( not copied )
				return sys_unexpected(copied.error());
		}
		else if( payload.size() != 0 )
			std::memcpy(result.wire->data() + encoded->size, payload.data(), payload.size());

		result.buffers = {
			const_buffer(result.wire->data(), result.wire->size())
		};
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

sys_expected<std::vector<prepared_frame>> frame_builder::prepare_message
(message_type type, std::span<const const_buffer> buffers, write_options options) const noexcept
{
	try {
		if( type != message_type::text and type != message_type::binary )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		size_t body_size = 0;
		utf8_validator utf8;

		for(const auto &buffer : buffers)
		{
			if( buffer.size() > std::numeric_limits<size_t>::max() - body_size )
				return sys_unexpected(make_system_error_code(std::errc::value_too_large));

			if( buffer.size() == 0 )
				continue;

			const auto *data = static_cast<const std::byte *>(buffer.data());
			if( data == nullptr )
				return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

			body_size += buffer.size();
			if( type == message_type::text and
				not utf8.consume(std::string_view(reinterpret_cast<const char*>(data), buffer.size())) )
				return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
		}
		if( m_max_message_size != 0 and body_size > m_max_message_size )
			return sys_unexpected(make_error_code(errc::message_too_big));

		if( type == message_type::text and not utf8.complete() )
			return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));

		if( options.compression != compression_mode::automatic and
			options.compression != compression_mode::enabled and
			options.compression != compression_mode::disabled )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		bool compress = false;
		if( options.compression == compression_mode::enabled )
		{
			if( not m_compression.enabled )
				return sys_unexpected(make_error_code(errc::unsupported_extension));
			compress = true;
		}
		else if( options.compression == compression_mode::automatic and
				 m_compression.enabled and body_size >= m_compression_config.min_message_size )
		{
			compress = type == message_type::text ?
				m_compression_config.compress_text : m_compression_config.compress_binary;
		}
		std::shared_ptr<std::vector<std::byte>> transformed_owner;
		std::vector<const_buffer> transformed_buffers;
		std::span<const const_buffer> payload_buffers = buffers;

		if( compress )
		{
			auto compressed = deflate_message(buffers,
				m_compression.outgoing_window_bits, m_compression_config.level
			);
			if( not compressed )
				return sys_unexpected(compressed.error());

			transformed_owner = std::make_shared
				<std::vector<std::byte>>(std::move(*compressed));

			transformed_buffers = {
				const_buffer(transformed_owner->data(), transformed_owner->size())
			};
			payload_buffers = transformed_buffers;
		}
		const auto wire_body_size = transformed_owner ?
			transformed_owner->size() : body_size;

		std::vector<prepared_frame> frames;
		const auto frame_count = wire_body_size == 0 or
			m_fragment_size == 0 ? 1 : 1 + (wire_body_size - 1) / m_fragment_size;

		if( frame_count > frames.max_size() )
			return sys_unexpected(make_system_error_code(std::errc::value_too_large));
		frames.reserve(frame_count);

		size_t offset = 0;
		size_t buffer_index = 0;
		size_t buffer_offset = 0;
		bool first = true;
		do {
			const auto remaining = wire_body_size - offset;
			const auto payload_size = m_fragment_size == 0 ?
				remaining : std::min(remaining, m_fragment_size);

			frame_header header {
				.fin = payload_size == remaining,
				.op = first ?
					(type == message_type::text ? opcode::text : opcode::binary) :
					opcode::continuation,
				.payload_size = payload_size,
			};
			if( first and compress )
				header.rsv = reserved_bit::rsv1;

			if( m_role == role::client )
			{
				masking_key key;
				auto random = secure_random_bytes (
					mutable_buffer(key.bytes.data(), key.bytes.size())
				);
				if( not random )
					return sys_unexpected(random.error());
				header.mask = key;
			}
			frame_codec_config codec_config {
				.local_role = m_role, .max_frame_size = m_max_frame_size
			};
			if( compress )
				codec_config.allowed_rsv = reserved_bit::rsv1;

			auto encoded = encode_frame_header(header, codec_config);
			if( not encoded )
				return sys_unexpected(encoded.error());

			prepared_frame prepared;
			prepared.header_size = encoded->size;
			prepared.payload_size = payload_size;
			prepared.payload_owner = transformed_owner;

			prepared.application_size = compress ?
				(header.fin ? body_size : 0) : payload_size;

			prepared.wire = std::make_shared<std::vector<std::byte>>(
				encoded->size + (header.mask ? payload_size : 0)
			);
			std::memcpy(prepared.wire->data(), encoded->buffer().data(), encoded->size);

			const auto payload_buffer_count = std::min(payload_buffers.size(), payload_size);
			prepared.buffers.reserve(header.mask ? 1 : payload_buffer_count + 1);

			prepared.buffers.emplace_back(prepared.wire->data(),
				header.mask ? prepared.wire->size() : encoded->size
			);
			size_t frame_offset = 0;
			while(frame_offset < payload_size)
			{
				while( buffer_index < payload_buffers.size() and
					   buffer_offset == payload_buffers[buffer_index].size() )
				{
					++buffer_index;
					buffer_offset = 0;
				}
				if( buffer_index == payload_buffers.size() )
					return sys_unexpected(make_system_error_code(std::errc::io_error));

				const auto &source = payload_buffers[buffer_index];
				const auto available = source.size() - buffer_offset;

				const auto size = std::min(payload_size - frame_offset, available);
				const auto *data = static_cast<const std::byte*>(source.data()) + buffer_offset;

				if( header.mask )
				{
					auto copied = mask_copy (
						mutable_buffer(prepared.wire->data() + encoded->size + frame_offset, size),
						const_buffer(data, size), *header.mask, frame_offset
					);
					if( not copied )
						return sys_unexpected(copied.error());
				}
				else
					prepared.buffers.emplace_back(data, size);

				frame_offset += size;
				buffer_offset += size;
			}
			frames.emplace_back(std::move(prepared));
			offset += payload_size;
			first = false;
		}
		while( offset < wire_body_size );
		return frames;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

} //namespace riwo::websocket::detail
