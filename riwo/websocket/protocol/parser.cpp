// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parser.h"
#include "detail/utf8.h"

namespace riwo::websocket
{

class RIWO_DECL_HIDDEN frame_parser::impl
{
public:
	explicit impl(frame_codec_config value) :
		m_config(value) {}

	[[nodiscard]] size_t expected_header_size() const noexcept
	{
		if( m_header_size < 2 )
			return 2;

		const auto second = std::to_integer<uint8_t>(m_header_storage[1]);
		const auto length = second & 0x7F;

		const auto extended_size = length == 126 ?
			size_t {2} : length == 127 ? size_t {8} : size_t {0};

		return 2 + extended_size + ((second & 0x80) != 0 ? 4 : 0);
	}

	[[nodiscard]] error_code decode_header() noexcept
	{
		const auto first = std::to_integer<uint8_t>(m_header_storage[0]);
		const auto second = std::to_integer<uint8_t>(m_header_storage[1]);

		const auto length_code = second & 0x7F;
		const bool masked = (second & 0x80) != 0;

		m_header = {};
		m_header.fin = (first & 0x80) != 0;
		m_header.rsv = reserved_bits(static_cast<reserved_bit>((first >> 4) & 0x07));
		m_header.op = static_cast<opcode>(first & 0x0F);

		size_t offset = 2;
		if( length_code <= 125 )
			m_header.payload_size = length_code;

		else if( length_code == 126 )
		{
			m_header.payload_size =
				static_cast<uint64_t>(std::to_integer<uint8_t>(m_header_storage[offset])) << 8 |
				std::to_integer<uint8_t>(m_header_storage[offset + 1]);

			offset += 2;
			if( m_header.payload_size <= 125 )
				return make_error_code(protocol_errc::noncanonical_length);
		}
		else
		{
			if( (std::to_integer<uint8_t>(m_header_storage[offset]) & 0x80) != 0 )
				return make_error_code(protocol_errc::invalid_64bit_length);

			for(size_t index=0; index<8; index++)
			{
				m_header.payload_size = (m_header.payload_size << 8) |
					std::to_integer<uint8_t>(m_header_storage[offset + index]);
			}
			offset += 8;
			if( m_header.payload_size <= 0xFFFF )
				return make_error_code(protocol_errc::noncanonical_length);
		}
		if( masked )
		{
			masking_key key;
			std::copy_n(m_header_storage.data() + offset,
				key.bytes.size(), key.bytes.data()
			);
			m_header.mask = key;
		}
		if( not is_known_opcode(m_header.op) )
			return make_error_code(protocol_errc::reserved_opcode);

		const auto rsv = m_header.rsv.value<uint8_t>();
		const auto allowed_rsv = m_config.allowed_rsv.value<uint8_t>();

		if( (rsv & ~allowed_rsv) != 0 )
			return make_error_code(protocol_errc::unexpected_rsv);

		if( m_config.local_role == role::server and not masked )
			return make_error_code(protocol_errc::missing_mask);

		if( m_config.local_role == role::client and masked )
			return make_error_code(protocol_errc::unexpected_mask);

		if( is_control_opcode(m_header.op) )
		{
			if( not m_header.fin )
				return make_error_code(protocol_errc::fragmented_control_frame);

			if( m_header.payload_size > 125 )
				return make_error_code(protocol_errc::ctrl_payload_too_large);
		}
		if( m_config.max_frame_size != 0 and m_header.payload_size > m_config.max_frame_size )
			return make_error_code(protocol_errc::frame_too_large);

		if( m_header.op == opcode::continuation )
		{
			if( not m_fragmented_message )
				return make_error_code(protocol_errc::unexpected_continuation);
		}
		else if( is_data_opcode(m_header.op) and m_fragmented_message )
			return make_error_code(protocol_errc::data_during_fragmentation);
		return {};
	}

	void commit_fragmentation() noexcept
	{
		if( m_header.op == opcode::continuation )
		{
			if( m_header.fin )
				m_fragmented_message = false;
		}
		else if( is_data_opcode(m_header.op) and not m_header.fin )
			m_fragmented_message = true;
	}

	void finish_frame() noexcept
	{
		m_header_size = 0;
		m_payload_remaining = 0;
		m_reading_payload = false;
	}

	[[nodiscard]] sys_expected<frame_parse_result> fail(error_code error) noexcept
	{
		if( not m_error )
			m_error = error;
		return sys_unexpected(m_error);
	}

public:
	frame_codec_config m_config;
	frame_header m_header {};

	std::array<std::byte,14> m_header_storage {};
	size_t m_header_size = 0;
	uint64_t m_payload_remaining = 0;

	bool m_reading_payload = false;
	bool m_fragmented_message = false;
	error_code m_error {};
};

frame_parser::frame_parser(frame_codec_config config) :
	m_impl(new impl(config))
{

}

frame_parser::~frame_parser()
{
	delete m_impl;
}

frame_parser::frame_parser(frame_parser &&other) noexcept :
	m_impl(std::exchange(other.m_impl, nullptr))
{

}

frame_parser &frame_parser::operator=(frame_parser &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = std::exchange(other.m_impl, nullptr);
	return *this;
}

sys_expected<frame_parse_result> frame_parser::parse(const mutable_buffer &input) noexcept
{
	if( not m_impl )
		return sys_unexpected(make_system_error_code(std::errc::operation_not_permitted));

	if( m_impl->m_error )
		return sys_unexpected(m_impl->m_error);

	frame_parse_result result;
	auto *data = static_cast<std::byte*>(input.data());
	size_t input_offset = 0;

	if( not m_impl->m_reading_payload )
	{
		if( m_impl->m_header_size < 2 )
		{
			const auto count = std::min(input.size(), 2 - m_impl->m_header_size);
			if( count != 0 )
			{
				std::memcpy(m_impl->m_header_storage.data() + m_impl->m_header_size, data, count);
				m_impl->m_header_size += count;
				input_offset += count;
			}
			if( m_impl->m_header_size < 2 )
			{
				result.consumed = input_offset;
				return result;
			}
		}
		const auto expected_size = m_impl->expected_header_size();
		const auto available = input.size() - input_offset;
		const auto count = std::min(available, expected_size - m_impl->m_header_size);

		if( count != 0 )
		{
			std::memcpy(m_impl->m_header_storage.data() + m_impl->m_header_size,
				data + input_offset, count
			);
			m_impl->m_header_size += count;
			input_offset += count;
		}
		result.consumed = input_offset;
		if( m_impl->m_header_size < expected_size )
			return result;

		if( auto error = m_impl->decode_header() )
			return m_impl->fail(error);

		m_impl->commit_fragmentation();
		m_impl->m_payload_remaining = m_impl->m_header.payload_size;
		m_impl->m_reading_payload = true;
		result.header_ready = true;

		if( m_impl->m_payload_remaining == 0 )
		{
			m_impl->finish_frame();
			result.frame_finished = true;
			return result;
		}
	}
	const auto available = input.size() - input_offset;
	const auto count = static_cast<size_t>(std::min<uint64_t> (
		m_impl->m_payload_remaining, available
	));
	if( count != 0 )
	{
		result.payload_offset = m_impl->m_header.payload_size - m_impl->m_payload_remaining;
		result.payload = mutable_buffer(data + input_offset, count);
		result.consumed += count;
		m_impl->m_payload_remaining -= count;
	}
	if( m_impl->m_payload_remaining == 0 )
	{
		m_impl->finish_frame();
		result.frame_finished = true;
	}
	return result;
}

const frame_header &frame_parser::header() const noexcept
{
	static const frame_header empty;
	return m_impl ? m_impl->m_header : empty;
}

uint64_t frame_parser::payload_remaining() const noexcept
{
	return m_impl ? m_impl->m_payload_remaining : 0;
}

frame_codec_config frame_parser::config() const noexcept
{
	return m_impl ? m_impl->m_config : frame_codec_config {};
}

bool frame_parser::failed() const noexcept
{
	return not m_impl or static_cast<bool>(m_impl->m_error);
}

error_code frame_parser::last_error() const noexcept
{
	return m_impl ? m_impl->m_error :
		make_system_error_code(std::errc::operation_not_permitted);
}

frame_parser &frame_parser::reset() noexcept
{
	if( not m_impl )
		return *this;

	m_impl->m_header = {};
	m_impl->m_header_storage = {};
	m_impl->m_header_size = 0;
	m_impl->m_payload_remaining = 0;
	m_impl->m_reading_payload = false;
	m_impl->m_fragmented_message = false;
	m_impl->m_error.clear();
	return *this;
}

sys_expected<close_payload_view> decode_close_payload(const const_buffer &payload) noexcept
{
	if( payload.size() == 0 )
		return close_payload_view {};

	if( payload.size() == 1 or payload.size() > 125 )
		return sys_unexpected(make_error_code(protocol_errc::invalid_close_payload));

	const auto *data = static_cast<const std::byte*>(payload.data());
	const auto code = static_cast<uint16_t>(
		static_cast<uint16_t>(std::to_integer<uint8_t>(data[0])) << 8 |
		std::to_integer<uint8_t>(data[1])
	);
	if( not is_valid_close_code(code) )
		return sys_unexpected(make_error_code(protocol_errc::invalid_close_payload));

	const std::string_view reason (
		reinterpret_cast<const char*>(data + 2), payload.size() - 2
	);
	if( not detail::is_valid_utf8(reason) )
		return sys_unexpected(make_error_code(protocol_errc::invalid_utf8));
	return close_payload_view { code, reason };
}

} //namespace riwo::websocket
