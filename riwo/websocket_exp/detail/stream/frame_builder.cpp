// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp/detail/stream/frame_builder.h>
#include <riwo/websocket_exp/detail/permessage_deflate.h>

#include <riwo/websocket_exp/protocol/detail/utf8.h>
#include <riwo/websocket_exp/protocol/generator.h>

#include <riwo/core/algorithm/secure_random.h>

namespace riwo::websocket_exp::detail
{

prepared_frame_buffers::prepared_frame_buffers(const prepared_frame &frame)
{
	if( frame.wire )
	{
		m_inline[0] = const_buffer(frame.wire->data(), frame.wire->size());
		m_buffers = std::span(m_inline.data(), 1);
	}
	else if( frame.additional_payloads.empty() )
	{
		m_inline[0] = const_buffer(frame.header.data(), frame.header_size);
		m_inline[1] = frame.payload;
		m_buffers = std::span(m_inline.data(), frame.payload.size() == 0 ? 1 : 2);
	}
	else
	{
		m_overflow.reserve(frame.additional_payloads.size() + 2);
		m_overflow.emplace_back(frame.header.data(), frame.header_size);
		m_overflow.push_back(frame.payload);
		m_overflow.insert(m_overflow.end(),
			frame.additional_payloads.begin(), frame.additional_payloads.end()
		);
		m_buffers = m_overflow;
	}
}

std::span<const const_buffer> prepared_frame_buffers::get() const noexcept
{
	return m_buffers;
}

bool prepared_message::empty() const noexcept
{
	return not first.has_value();
}

size_t prepared_message::size() const noexcept
{
	return first ? additional.size() + 1 : 0;
}

prepared_frame &prepared_message::front() noexcept
{
	return *first;
}

const prepared_frame &prepared_message::front() const noexcept
{
	return *first;
}

prepared_frame &prepared_message::operator[](size_t index) noexcept
{
	return index == 0 ? *first : additional[index - 1];
}

const prepared_frame &prepared_message::operator[](size_t index) const noexcept
{
	return index == 0 ? *first : additional[index - 1];
}

void prepared_message::reserve(size_t count)
{
	if( count > 1 )
		additional.reserve(count - 1);
}

void prepared_message::push_back(prepared_frame frame)
{
	if( not first )
		first = std::move(frame);
	else
		additional.push_back(std::move(frame));
}

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

	m_deflater_error.clear();
	if( m_compression.enabled )
	{
		m_deflater_error = m_deflater.reset (
			m_compression.outgoing_window_bits, m_compression_config.level
		);
	}
	else
		m_deflater = permessage_deflater {};

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

		const bool borrowed = borrow_payload and not header.mask;
		prepared_frame result {};

		result.header_size = encoded->size;
		result.payload_size = payload.size();
		result.application_size = payload.size();

		if( borrowed )
		{
			std::memcpy(result.header.data(), encoded->buffer().data(), encoded->size);
			result.payload = payload;
			return result;
		}
		result.wire = std::make_shared<std::vector<std::byte>>(
			encoded->size + payload.size()
		);
		std::memcpy(result.wire->data(), encoded->buffer().data(), encoded->size);
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

		const bool borrowed = borrow_payload and not header.mask;
		prepared_frame result {};

		result.header_size = encoded->size;
		result.payload_size = payload.size();
		result.application_size = payload.size();

		if( borrowed )
		{
			std::memcpy(result.header.data(), encoded->buffer().data(), encoded->size);
			result.payload = payload;
			return result;
		}
		result.wire = std::make_shared<std::vector<std::byte>>(
			encoded->size + payload.size()
		);
		std::memcpy(result.wire->data(), encoded->buffer().data(), encoded->size);
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
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));
}

sys_expected<prepared_message> frame_builder::prepare_message
(message_type type, std::span<const const_buffer> buffers, write_options options) const noexcept
{
	try {
		if( type != message_type::text and type != message_type::binary )
			return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

		size_t body_size = 0;
		utf8_validator utf8 {};

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
		std::shared_ptr<std::vector<std::byte>> transformed_owner {};
		std::vector<const_buffer> transformed_buffers {};
		std::span<const const_buffer> payload_buffers = buffers;

		if( compress )
		{
			if( m_deflater_error )
				return sys_unexpected(m_deflater_error);

			auto compressed = m_deflater.deflate(buffers);
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

		prepared_message frames {};
		const auto frame_count = wire_body_size == 0 or
			m_fragment_size == 0 ? 1 : 1 + (wire_body_size - 1) / m_fragment_size;

		if( frame_count > frames.additional.max_size() )
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

			if( header.mask )
			{
				prepared.wire = std::make_shared<std::vector<std::byte>>(
					encoded->size + payload_size
				);
				std::memcpy(prepared.wire->data(), encoded->buffer().data(), encoded->size);
			}
			else
				std::memcpy(prepared.header.data(), encoded->buffer().data(), encoded->size);

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
				{
					const_buffer segment(data, size);
					if( prepared.payload.size() == 0 )
						prepared.payload = segment;
					else
						prepared.additional_payloads.push_back(segment);
				}

				frame_offset += size;
				buffer_offset += size;
			}
			frames.push_back(std::move(prepared));
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

} //namespace riwo::websocket_exp::detail
