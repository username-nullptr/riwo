// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/detail/permessage_deflate.h>

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
# include <zlib.h>
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT

namespace riwo::websocket::detail
{

struct RIWO_DECL_HIDDEN permessage_inflater::impl
{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	z_stream stream {};
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT

	uint8_t window_bits = 15;
	bool no_context_takeover = true;

	bool initialized = false;
	size_t message_input_size = 0;

	~impl()
	{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
		if( initialized )
			inflateEnd(&stream);
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
	}
};

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
namespace
{

optional<uint8_t> parse_window_bits(const optional<std::string> &value) noexcept
{
	if( not value )
		return nullopt;

	if( value->size() == 1 and ((*value)[0] == '8' or (*value)[0] == '9') )
		return static_cast<uint8_t>((*value)[0] - '0');

	if( value->size() == 2 and (*value)[0] == '1' and (*value)[1] >= '0' and (*value)[1] <= '5' )
		return static_cast<uint8_t>(10 + (*value)[1] - '0');

	return nullopt;
}

sys_expected<permessage_deflate_parameters> parse_parameters(const extension &value, bool response) noexcept
{
	if( value.name != "permessage-deflate" )
		return sys_unexpected(make_error_code(errc::unsupported_extension));

	permessage_deflate_parameters result;
	bool server_no_context_takeover = false;
	bool client_no_context_takeover = false;

	bool server_max_window_bits = false;
	bool client_max_window_bits = false;

	for(const auto &parameter : value.parameters)
	{
		if( parameter.name == "server_no_context_takeover" )
		{
			if( parameter.value or std::exchange(server_no_context_takeover, true) )
				return sys_unexpected(make_error_code(errc::unsupported_extension));
			result.server_no_context_takeover = true;
		}
		else if( parameter.name == "client_no_context_takeover" )
		{
			if( parameter.value or std::exchange(client_no_context_takeover, true) )
				return sys_unexpected(make_error_code(errc::unsupported_extension));
			result.client_no_context_takeover = true;
		}
		else if( parameter.name == "server_max_window_bits" )
		{
			if( std::exchange(server_max_window_bits, true) )
				return sys_unexpected(make_error_code(errc::unsupported_extension));

			auto bits = parse_window_bits(parameter.value);
			if( not bits )
				return sys_unexpected(make_error_code(errc::unsupported_extension));
			result.server_max_window_bits = *bits;
		}
		else if( parameter.name == "client_max_window_bits" )
		{
			if( std::exchange(client_max_window_bits, true) )
				return sys_unexpected(make_error_code(errc::unsupported_extension));

			result.client_max_window_bits_present = true;
			if( parameter.value )
			{
				auto bits = parse_window_bits(parameter.value);
				if( not bits )
					return sys_unexpected(make_error_code(errc::unsupported_extension));
				result.client_max_window_bits = *bits;
			}
			else if( response )
				return sys_unexpected(make_error_code(errc::unsupported_extension));
		}
		else
			return sys_unexpected(make_error_code(errc::unsupported_extension));
	}
	return result;
}

bool response_matches_offer
(const permessage_deflate_parameters &response, const permessage_deflate_parameters &offer) noexcept
{
	if( response.client_max_window_bits_present )
	{
		if( not offer.client_max_window_bits_present or not response.client_max_window_bits )
			return false;

		if( offer.client_max_window_bits and
			*response.client_max_window_bits > *offer.client_max_window_bits )
			return false;
	}
	if( response.server_max_window_bits and offer.server_max_window_bits and
		*response.server_max_window_bits > *offer.server_max_window_bits )
		return false;
	return true;
}

} //namespace
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT

permessage_inflater::permessage_inflater() noexcept = default;

permessage_inflater::~permessage_inflater() = default;

permessage_inflater::permessage_inflater(permessage_inflater&&) noexcept = default;

permessage_inflater &permessage_inflater::operator=(permessage_inflater&&) noexcept = default;

error_code permessage_inflater::reset(uint8_t window_bits, bool no_context_takeover) noexcept
{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	if( window_bits < 8 or window_bits > 15 )
		return make_system_error_code(std::errc::invalid_argument);
	try {
		auto state = std::make_unique<impl>();
		state->window_bits = window_bits;
		state->no_context_takeover = no_context_takeover;

		const auto zlib_window_bits = window_bits == 8 ? 9 : window_bits;
		if( inflateInit2(&state->stream, -static_cast<int>(zlib_window_bits)) != Z_OK )
			return make_system_error_code(std::errc::io_error);

		state->initialized = true;
		m_impl = std::move(state);
		return {};
	}
	catch(const std::bad_alloc&) {
		return make_system_error_code(std::errc::not_enough_memory);
	}
	catch(...) {}
	return make_system_error_code(std::errc::io_error);

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(window_bits, no_context_takeover);
	return make_error_code(errc::unsupported_extension);
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

sys_expected<std::vector<std::byte>> permessage_inflater::inflate
(std::span<const std::byte> payload, size_t max_message_size) noexcept
{
	return inflate_chunk(payload, true, max_message_size);
}

sys_expected<std::vector<std::byte>> permessage_inflater::inflate_chunk
(std::span<const std::byte> payload, bool final, size_t max_output_size) noexcept
{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	try {
		if( not m_impl or not m_impl->initialized )
			return sys_unexpected(make_system_error_code(std::errc::io_error));

		if( payload.size() > std::numeric_limits<size_t>::max() - m_impl->message_input_size )
			return sys_unexpected(make_system_error_code(std::errc::value_too_large));

		m_impl->message_input_size += payload.size();

		std::vector<std::byte> result;
		std::array<std::byte, 16 * 1024> output {};

		constexpr std::array trailer {
			std::byte {0x00}, std::byte {0x00},
			std::byte {0xFF}, std::byte {0xFF}
		};
		auto consume = [&](std::span<const std::byte> input) -> error_code
		{
			size_t offset = 0;
			while( offset < input.size() )
			{
				const auto size = static_cast<uInt>(std::min<size_t>(
					input.size() - offset, std::numeric_limits<uInt>::max())
				);
				m_impl->stream.next_in = reinterpret_cast<Bytef*>(
					const_cast<std::byte*>(input.data() + offset)
				);
				m_impl->stream.avail_in = size;
				do {
					m_impl->stream.next_out = reinterpret_cast<Bytef*>(output.data());
					m_impl->stream.avail_out = static_cast<uInt>(output.size());

					const auto code = ::inflate(&m_impl->stream, Z_SYNC_FLUSH);
					const auto produced = output.size() - m_impl->stream.avail_out;

					if( code != Z_OK and code != Z_BUF_ERROR )
						return make_error_code(protocol_errc::invalid_compressed_payload);

					if( max_output_size != 0 and produced >
						max_output_size - std::min(result.size(), max_output_size) )
						return make_error_code(errc::message_too_big);

					result.insert(result.end(), output.begin(), output.begin() + produced);
					if( code == Z_BUF_ERROR and produced == 0 )
					{
						if( m_impl->stream.avail_in != 0 )
							return make_error_code(protocol_errc::invalid_compressed_payload);
						break;
					}
				}
				while( m_impl->stream.avail_in != 0 or m_impl->stream.avail_out == 0 );
				offset += size;
			}
			return {};
		};
		if( auto error = consume(payload) )
			return sys_unexpected(error);

		if( final )
		{
			if( m_impl->message_input_size == 0 )
				return sys_unexpected(make_error_code(protocol_errc::invalid_compressed_payload));

			if( auto error = consume(trailer) )
				return sys_unexpected(error);

			if( (m_impl->stream.data_type & 128) == 0 )
				return sys_unexpected(make_error_code(protocol_errc::invalid_compressed_payload));

			m_impl->message_input_size = 0;
			const auto zlib_window_bits = m_impl->window_bits == 8 ?
				9 : m_impl->window_bits;

			if( m_impl->no_context_takeover and
				inflateReset2(&m_impl->stream, -static_cast<int>(zlib_window_bits)) != Z_OK )
				return sys_unexpected(make_system_error_code(std::errc::io_error));
		}
		return result;
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(payload, final, max_output_size);
	return sys_unexpected(make_error_code(errc::unsupported_extension));
#endif //!RIWO_WEBSOCKET_ZLIB_SUPPORT
}

bool supported_extension_offers(std::span<const extension> extensions) noexcept
{
	if( extensions.empty() )
		return true;

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	return std::ranges::all_of(extensions, [](const auto &value) {
		return !!parse_parameters(value, false);
	});
#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	RIWO_UNUSED(extensions);
	return false;
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

bool supported_negotiated_extensions(std::span<const extension> extensions) noexcept
{
	if( extensions.empty() )
		return true;

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	return extensions.empty() or (extensions.size() == 1 and
		parse_parameters(extensions.front(), true).has_value()
	);
#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	RIWO_UNUSED(extensions);
	return false;
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

bool supported_extension_response
(std::span<const extension> response, std::span<const extension> offers) noexcept
{
	if( response.empty() )
		return true;

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	if( response.size() != 1 )
		return false;

	auto parsed_response = parse_parameters(response.front(), true);
	if( not parsed_response )
		return false;

	for(const auto &offer : offers)
	{
		auto parsed_offer = parse_parameters(offer, false);
		if( parsed_offer and response_matches_offer(*parsed_response, *parsed_offer) )
			return true;
	}
	return false;

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	RIWO_UNUSED(offers);
	return false;
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

sys_expected<extension> negotiate_permessage_deflate
(const extension &offer, const extension &policy) noexcept
{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	try {
		auto offered = parse_parameters(offer, false);
		auto configured = parse_parameters(policy, false);

		if( not offered )
			return sys_unexpected(offered.error());

		if( not configured )
			return sys_unexpected(configured.error());

		permessage_deflate_options selected;
		selected.server_no_context_takeover =
			offered->server_no_context_takeover or configured->server_no_context_takeover;

		selected.client_no_context_takeover =
			offered->client_no_context_takeover or configured->client_no_context_takeover;

		if( offered->server_max_window_bits or configured->server_max_window_bits )
		{
			selected.server_max_window_bits = std::min (
				offered->server_max_window_bits.value_or(15),
				configured->server_max_window_bits.value_or(15)
			);
		}
		if( offered->client_max_window_bits_present )
		{
			selected.client_max_window_bits = std::min (
				offered->client_max_window_bits.value_or(15),
				configured->client_max_window_bits.value_or(15)
			);
		}
		return permessage_deflate_extension(selected);
	}
	catch(const std::bad_alloc&) {
		return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {}
	return sys_unexpected(make_system_error_code(std::errc::io_error));

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(offer, policy);
	return sys_unexpected(make_error_code(errc::unsupported_extension));
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

sys_expected<permessage_deflate_runtime> make_permessage_deflate_runtime
(std::span<const extension> negotiated_extensions, role local_role) noexcept
{
	if( negotiated_extensions.empty() )
		return permessage_deflate_runtime {};

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	if( negotiated_extensions.size() != 1 )
		return sys_unexpected(make_error_code(errc::unsupported_extension));

	auto parameters = parse_parameters(negotiated_extensions.front(), true);
	if( not parameters )
		return sys_unexpected(parameters.error());

	permessage_deflate_runtime result;
	result.enabled = true;

	if( local_role == role::client )
	{
		result.outgoing_no_context_takeover = parameters->client_no_context_takeover;
		result.incoming_no_context_takeover = parameters->server_no_context_takeover;

		result.outgoing_window_bits = parameters->client_max_window_bits.value_or(15);
		result.incoming_window_bits = parameters->server_max_window_bits.value_or(15);
	}
	else
	{
		result.outgoing_no_context_takeover = parameters->server_no_context_takeover;
		result.incoming_no_context_takeover = parameters->client_no_context_takeover;

		result.outgoing_window_bits = parameters->server_max_window_bits.value_or(15);
		result.incoming_window_bits = parameters->client_max_window_bits.value_or(15);
	}
	return result;

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(negotiated_extensions, local_role);
	return sys_unexpected(make_error_code(errc::unsupported_extension));
#endif //RIWO_WEBSOCKET_ZLIB_SUPPORT
}

sys_expected<std::vector<std::byte>> deflate_message
(std::span<const const_buffer> buffers, uint8_t window_bits, int compression_level) noexcept
{
	sys_expected<std::vector<std::byte>> result = std::vector<std::byte>{};
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	try {
		if( window_bits < 8 or window_bits > 15 or compression_level < -1 or compression_level > 9 )
			return result.despair(make_system_error_code(std::errc::invalid_argument));

		const auto zlib_window_bits = window_bits == 8 ? 9 : window_bits;
		z_stream stream {};

		if( deflateInit2(&stream, compression_level, Z_DEFLATED,
			-static_cast<int>(zlib_window_bits), 8, Z_DEFAULT_STRATEGY) != Z_OK )
			return result.despair(make_system_error_code(std::errc::io_error));

		struct guard_t
		{
			z_stream &stream;
			~guard_t() {
				deflateEnd(&stream);
			}
		}
		guard {stream};

		result = std::vector<std::byte>{};
		std::array<std::byte, 16 * 1024> output {};

		auto pump = [&](int flush) -> bool
		{
			do {
				stream.next_out = reinterpret_cast<Bytef*>(output.data());
				stream.avail_out = static_cast<uInt>(output.size());
				const auto code = deflate(&stream, flush);

				if( code != Z_OK )
					return false;

				const auto produced = output.size() - stream.avail_out;
				result->insert(result->end(), output.begin(), output.begin() + produced);
			}
			while( stream.avail_in != 0 or stream.avail_out == 0 );
			return true;
		};

		for(const auto &buffer : buffers)
		{
			if( buffer.size() != 0 and buffer.data() == nullptr )
				return result.despair(make_system_error_code(std::errc::invalid_argument));

			auto *input = static_cast<const std::byte*>(buffer.data());
			auto remaining = buffer.size();

			while( remaining != 0 )
			{
				const auto size = static_cast<uInt>(std::min<size_t>(
					remaining, std::numeric_limits<uInt>::max())
				);
				stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input));
				stream.avail_in = size;

				if( not pump(Z_NO_FLUSH) )
					return result.despair(make_system_error_code(std::errc::io_error));

				input += size;
				remaining -= size;
			}
		}
		stream.next_in = nullptr;
		stream.avail_in = 0;

		if( not pump(Z_SYNC_FLUSH) )
			return result.despair(make_system_error_code(std::errc::io_error));

		constexpr std::array trailer {
			std::byte {0x00}, std::byte {0x00},
			std::byte {0xFF}, std::byte {0xFF}
		};
		if( result->size() < trailer.size() or
			not std::equal(trailer.begin(), trailer.end(), result->end() - trailer.size()) )
			return result.despair(make_system_error_code(std::errc::io_error));

		result->resize(result->size() - trailer.size());
		return result;
	}
	catch(const std::bad_alloc&) {
		result.despair(make_system_error_code(std::errc::not_enough_memory));
	}
	catch(...) {
		result.despair(make_system_error_code(std::errc::io_error));
	}
	return result;

#else //RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(buffers, window_bits, compression_level);
	return result.despair(make_error_code(errc::unsupported_extension));
#endif //!RIWO_WEBSOCKET_ZLIB_SUPPORT
}

sys_expected<std::vector<std::byte>> inflate_message
(std::span<const std::byte> payload, size_t max_message_size, uint8_t window_bits) noexcept
{
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
	permessage_inflater inflater;
	if( auto error = inflater.reset(window_bits, true) )
		return sys_unexpected(error);
	return inflater.inflate(payload, max_message_size);

#else //!RIWO_WEBSOCKET_ZLIB_SUPPORT
	ignore_unused(payload, max_message_size, window_bits);
	return sys_unexpected(make_error_code(errc::unsupported_extension));
#endif //!RIWO_WEBSOCKET_ZLIB_SUPPORT
}

} //namespace riwo::websocket::detail
