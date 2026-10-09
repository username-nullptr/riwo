// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "types.h"

namespace riwo::websocket { namespace
{

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
// Boost.System deliberately gives error_category a protected non-virtual
// destructor. This static final category is never deleted polymorphically, but
// GCC still diagnoses the supported inheritance pattern under
// -Wnon-virtual-dtor.
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif //BOOST & GNU

class RIWO_DECL_HIDDEN websocket_protocol_error_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(websocket_protocol_error_category)

public:
	websocket_protocol_error_category() = default;
#if !RIWO_USING_BOOST_ASIO
	~websocket_protocol_error_category() override = default;
#endif //RIWO_USING_BOOST_ASIO

public:
	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::websocket::protocol";
	}
	[[nodiscard]] std::string message(int code) const override
	{
		switch(static_cast<protocol_errc>(code))
		{
		case protocol_errc::reserved_opcode:
			return "Reserved WebSocket opcode";

		case protocol_errc::unexpected_rsv:
			return "Unexpected WebSocket reserved bit";

		case protocol_errc::unexpected_mask:
			return "Unexpected WebSocket masking key";

		case protocol_errc::missing_mask:
			return "Missing WebSocket masking key";

		case protocol_errc::noncanonical_length:
			return "Non-canonical WebSocket payload length";

		case protocol_errc::invalid_64bit_length:
			return "Invalid 64-bit WebSocket payload length";

		case protocol_errc::fragmented_control_frame:
			return "Fragmented WebSocket control frame";

		case protocol_errc::ctrl_payload_too_large:
			return "WebSocket control payload too large";

		case protocol_errc::frame_too_large:
			return "WebSocket frame too large";

		case protocol_errc::invalid_close_payload:
			return "Invalid WebSocket Close payload";

		case protocol_errc::invalid_utf8:
			return "Invalid WebSocket UTF-8";

		case protocol_errc::unexpected_continuation:
			return "Unexpected WebSocket continuation frame";

		case protocol_errc::data_during_fragmentation:
			return "WebSocket data frame during fragmented message";

		case protocol_errc::invalid_compressed_payload:
			return "Invalid compressed WebSocket message payload";

		default: break;
		}
		return "Unknown WebSocket protocol error";
	}
};

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif //BOOST & GNU

} //namespace

close_frame::close_frame(close_code value, std::string text) :
	code(static_cast<uint16_t>(value)), reason(std::move(text))
{

}

close_frame::close_frame(uint16_t value, std::string text) :
	code(value), reason(std::move(text))
{

}

extension permessage_deflate_extension()
{
	permessage_deflate_options options;
	options.server_no_context_takeover = true;
	options.client_no_context_takeover = true;
	return permessage_deflate_extension(options);
}

extension permessage_deflate_extension(const permessage_deflate_options &options)
{
	extension result {.name = "permessage-deflate"};
	if( options.server_no_context_takeover )
		result.parameters.push_back({.name = "server_no_context_takeover"});

	if( options.client_no_context_takeover )
		result.parameters.push_back({.name = "client_no_context_takeover"});

	if( options.server_max_window_bits )
	{
		result.parameters.push_back ({
			.name = "server_max_window_bits",
			.value = std::to_string(*options.server_max_window_bits),
		});
	}
	if( options.offer_client_max_window_bits or options.client_max_window_bits )
	{
		result.parameters.push_back ({
			.name = "client_max_window_bits",
			.value = options.client_max_window_bits ?
				optional(std::to_string(*options.client_max_window_bits)) :
				nullopt,
		});
	}
	return result;
}

bool is_permessage_deflate_extension(const extension &value) noexcept
{
	if( value.name != "permessage-deflate" )
		return false;

	bool server_no_context_takeover = false;
	bool client_no_context_takeover = false;

	bool server_max_window_bits = false;
	bool client_max_window_bits = false;

	auto valid_window_bits = [](const std::string &text) noexcept
	{
		return (text.size() == 1 and (text[0] == '8' or text[0] == '9')) or
			   (text.size() == 2 and text[0] == '1' and text[1] >= '0' and text[1] <= '5');
	};
	for(const auto &parameter : value.parameters)
	{
		if( parameter.name == "server_no_context_takeover" )
		{
			if( parameter.value or std::exchange(server_no_context_takeover, true) )
				return false;
		}
		else if( parameter.name == "client_no_context_takeover" )
		{
			if( parameter.value or std::exchange(client_no_context_takeover, true) )
				return false;
		}
		else if( parameter.name == "server_max_window_bits" )
		{
			if( not parameter.value or not valid_window_bits(*parameter.value) or
				std::exchange(server_max_window_bits, true) )
				return false;
		}
		else if( parameter.name == "client_max_window_bits" )
		{
			if( (parameter.value and not valid_window_bits(*parameter.value)) or
				std::exchange(client_max_window_bits, true) )
				return false;
		}
		else
			return false;
	}
	return true;
}

const error_category_t &protocol_error_category() noexcept
{
	static websocket_protocol_error_category category;
	return category;
}

error_code make_error_code(protocol_errc value) noexcept
{
	return { static_cast<int>(value), protocol_error_category() };
}

bool is_known_opcode(opcode value) noexcept
{
	switch(value)
	{
	case opcode::continuation:
	case opcode::text:
	case opcode::binary:
	case opcode::close:
	case opcode::ping:
	case opcode::pong:
		return true;
	default:
		break;
	}
	return false;
}

bool is_control_opcode(opcode value) noexcept
{
	return value == opcode::close or value == opcode::ping or value == opcode::pong;
}

bool is_data_opcode(opcode value) noexcept
{
	return value == opcode::text or value == opcode::binary;
}

bool is_valid_close_code(uint16_t value) noexcept
{
	if( value >= 3000 and value < 5000 )
		return true;

	switch(static_cast<close_code>(value))
	{
	case close_code::normal_closure:
	case close_code::going_away:
	case close_code::protocol_error:
	case close_code::unsupported_data:
	case close_code::invalid_payload:
	case close_code::policy_violation:
	case close_code::message_too_big:
	case close_code::mandatory_extension:
	case close_code::internal_error:
	case close_code::service_restart:
	case close_code::try_again_later:
	case close_code::bad_gateway:
		return true;
	default:
		break;
	}
	return false;
}

close_code close_code_for(protocol_errc value) noexcept
{
	switch(value)
	{
	case protocol_errc::invalid_utf8   : return close_code::invalid_payload;
	case protocol_errc::frame_too_large: return close_code::message_too_big;
	default: break;
	}
	return close_code::protocol_error;
}

} //namespace riwo::websocket
